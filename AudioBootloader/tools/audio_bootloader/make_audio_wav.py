#!/usr/bin/env python3

from __future__ import annotations

import argparse
import array
import math
import os
import shlex
import struct
import subprocess
import sys
import tempfile
import wave
import zlib
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve()
TOOLS_DIR = SCRIPT_PATH.parent
AUDIO_BOOTLOADER_ROOT = SCRIPT_PATH.parents[2]
WORKSPACE_ROOT = AUDIO_BOOTLOADER_ROOT.parent if (AUDIO_BOOTLOADER_ROOT.parent / "Sketches").exists() else AUDIO_BOOTLOADER_ROOT

APP_BASE = 0x10020000
APP_SLOT_SIZE = 0x00200000
APP_VECTOR_OFFSET = 0x00003000
BUFFERED_PAYLOAD_LIMIT = 256 * 1024
DEFAULT_CPU_HZ = 150_000_000
MIN_CPU_HZ = 50_000_000
MAX_CPU_HZ = 300_000_000

HEADER_MAGIC0 = 0x32485041  # 2HPA
HEADER_MAGIC1 = 0x5544424C  # UDBL
HEADER_VERSION = 1
HEADER_SIZE = 256
PACKET_SIZE = 256
IMAGE_FLAG_COMPRESSED_ZLIB = 1 << 0

SAMPLE_RATE = 48000
ZERO_PERIOD = 9
ONE_PERIOD = 18
BLANK_PERIOD = 54
PAGE_GROUP_BYTES = 4096
INTRO_SILENCE_SECONDS = 0.35
INTRO_BLANK_SECONDS = 0.45
OUTRO_BLANK_SECONDS = 0.80
GROUP_BLANK_SECONDS = 0.35


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compile an Arduino sketch for the RP2350 app slot and emit an FSK WAV")
    parser.add_argument("sketch", type=Path, help="Path to the .ino sketch")
    parser.add_argument("--fqbn", default="rp2040:rp2040:rpipico2", help="arduino-cli FQBN")
    parser.add_argument("--output", type=Path, help="Output WAV path")
    parser.add_argument("--build-dir", type=Path, help="Explicit build directory")
    parser.add_argument("--keep-build-dir", action="store_true", help="Do not delete the temporary build directory")
    parser.add_argument("--arduino-cli", default="arduino-cli", help="arduino-cli executable")
    parser.add_argument("--sample-rate", type=int, default=SAMPLE_RATE)
    parser.add_argument("--zero-period", type=int, default=ZERO_PERIOD)
    parser.add_argument("--one-period", type=int, default=ONE_PERIOD)
    parser.add_argument("--blank-period", type=int, default=BLANK_PERIOD)
    parser.add_argument("--group-blank", type=float, default=GROUP_BLANK_SECONDS)
    parser.add_argument("--compression", choices=("auto", "off", "zlib"), default="auto")
    parser.add_argument("--cpu-hz", type=lambda x: int(x, 0), help="CPU frequency in Hz to encode into the image header and apply to the compile FQBN")
    parser.add_argument("--app-base", type=lambda x: int(x, 0), default=APP_BASE)
    parser.add_argument("--slot-size", type=lambda x: int(x, 0), default=APP_SLOT_SIZE)
    parser.add_argument("--vector-offset", type=lambda x: int(x, 0), default=APP_VECTOR_OFFSET)
    return parser.parse_args()


def run_checked(cmd: list[str], **kwargs) -> None:
    subprocess.run(cmd, check=True, **kwargs)


def existing_library_dirs() -> list[Path]:
    candidates = [
        WORKSPACE_ROOT / "Sketches" / "lib",
        AUDIO_BOOTLOADER_ROOT / "Pico-Sketches" / "lib",
        WORKSPACE_ROOT / "DaisySP_Teensy-main",
        AUDIO_BOOTLOADER_ROOT / "DaisySP_Teensy-main",
    ]
    seen: set[Path] = set()
    libraries: list[Path] = []
    for path in candidates:
        if not path.exists() or path in seen:
            continue
        libraries.append(path)
        seen.add(path)
    return libraries


def split_fqbn(fqbn: str) -> tuple[str, str, str, dict[str, str]]:
    parts = fqbn.split(":", 3)
    if len(parts) < 3:
        raise ValueError(f"Invalid FQBN: {fqbn}")
    package, arch, board = parts[0], parts[1], parts[2]
    options: dict[str, str] = {}
    if len(parts) == 4 and parts[3]:
        for item in parts[3].split(","):
            if not item:
                continue
            if "=" not in item:
                raise ValueError(f"Invalid FQBN option: {item}")
            key, value = item.split("=", 1)
            options[key] = value
    return package, arch, board, options


def join_fqbn(package: str, arch: str, board: str, options: dict[str, str]) -> str:
    fqbn = f"{package}:{arch}:{board}"
    if options:
        fqbn += ":" + ",".join(f"{key}={value}" for key, value in options.items())
    return fqbn


def default_cpu_hz_for_fqbn(fqbn: str) -> int:
    _, _, board, _ = split_fqbn(fqbn)
    if board == "rpipico2":
        return DEFAULT_CPU_HZ
    return 125_000_000


def valid_cpu_hz(cpu_hz: int) -> bool:
    return MIN_CPU_HZ <= cpu_hz <= MAX_CPU_HZ and cpu_hz % 1000 == 0


def infer_cpu_hz_from_fqbn(fqbn: str) -> int:
    package, arch, board, options = split_fqbn(fqbn)
    if "freq" not in options:
        return default_cpu_hz_for_fqbn(join_fqbn(package, arch, board, options))
    freq = int(options["freq"], 0)
    return freq if freq >= 1000 else freq * 1_000_000


def inject_cpu_hz_into_fqbn(fqbn: str, cpu_hz: int) -> str:
    if cpu_hz % 1_000_000 != 0:
        raise ValueError("--cpu-hz must be a whole MHz value to map onto Arduino-Pico's freq menu")
    package, arch, board, options = split_fqbn(fqbn)
    options["freq"] = str(cpu_hz // 1_000_000)
    return join_fqbn(package, arch, board, options)


def resolve_fqbn_and_cpu_hz(fqbn: str, cpu_hz: int | None) -> tuple[str, int]:
    resolved_cpu_hz = infer_cpu_hz_from_fqbn(fqbn) if cpu_hz is None else cpu_hz
    if not valid_cpu_hz(resolved_cpu_hz):
        raise ValueError(f"Unsupported CPU frequency: {resolved_cpu_hz} Hz")
    resolved_fqbn = inject_cpu_hz_into_fqbn(fqbn, resolved_cpu_hz)
    return resolved_fqbn, resolved_cpu_hz


def build_override_pattern(app_base: int) -> str:
    python = shlex.quote(sys.executable)
    render = shlex.quote(str(TOOLS_DIR / "render_linker.py"))
    return (
        f"{python} -I {render} "
        '--input "{runtime.platform.path}/lib/{build.chip}/memmap_default.ld" '
        '--out "{build.path}/memmap_default.ld" '
        "--flash-length {build.flash_length} "
        "--eeprom-start {build.eeprom_start} "
        "--fs-start {build.fs_start} "
        "--fs-end {build.fs_end} "
        '--ram-length "{build.ram_length}" '
        '--psram-length "{build.psram_length}" '
        f"--app-base 0x{app_base:08x}"
    )


def compile_sketch(args: argparse.Namespace, build_dir: Path) -> Path:
    build_properties = [
        f"recipe.hooks.linking.prelink.1.pattern={build_override_pattern(args.app_base)}",
        f"build.flash_length={args.slot_size}",
    ]

    cmd = [
        args.arduino_cli,
        "compile",
        "--fqbn",
        getattr(args, "resolved_fqbn", args.fqbn),
        "--output-dir",
        str(build_dir),
    ]
    for library_dir in existing_library_dirs():
        cmd.extend(["--libraries", str(library_dir)])
    for prop in build_properties:
      cmd.extend(["--build-property", prop])
    cmd.append(str(args.sketch.parent))
    run_checked(cmd, cwd=WORKSPACE_ROOT)

    bins = sorted(build_dir.glob("*.bin"))
    if not bins:
        raise FileNotFoundError("No .bin output produced by arduino-cli")
    return bins[0]


def align_image(image: bytes, packet_size: int) -> bytes:
    if len(image) % packet_size == 0:
        return image
    pad = packet_size - (len(image) % packet_size)
    return image + (b"\x00" * pad)


def make_header(
    image_size: int,
    image_crc32: int,
    app_base: int,
    vector_offset: int,
    flags: int,
    payload_size: int,
    encoded_size: int,
    cpu_hz: int,
) -> bytes:
    header = struct.pack(
        "<IIIIIIIIIIII",
        HEADER_MAGIC0,
        HEADER_MAGIC1,
        HEADER_VERSION,
        HEADER_SIZE,
        image_size,
        image_crc32,
        app_base,
        vector_offset,
        flags,
        payload_size,
        encoded_size,
        cpu_hz,
    )
    return header.ljust(HEADER_SIZE, b"\x00")


def package_image(image: bytes, app_base: int, vector_offset: int, compression: str, cpu_hz: int = DEFAULT_CPU_HZ) -> bytes:
    aligned = align_image(image, PACKET_SIZE)
    image_crc32 = zlib.crc32(aligned) & 0xFFFFFFFF
    payload = aligned
    flags = 0
    encoded_size = len(aligned)

    compressed = zlib.compress(aligned, 9)
    compressed_payload = align_image(compressed, PACKET_SIZE)
    use_compressed = False
    if compression == "zlib":
        if len(compressed_payload) > BUFFERED_PAYLOAD_LIMIT:
            raise ValueError("Compressed payload exceeds bootloader buffered payload limit")
        use_compressed = True
    elif compression == "auto":
        use_compressed = (
            len(compressed_payload) < len(aligned) and
            len(compressed_payload) <= BUFFERED_PAYLOAD_LIMIT
        )

    if use_compressed:
        payload = compressed_payload
        flags |= IMAGE_FLAG_COMPRESSED_ZLIB
        encoded_size = len(compressed)

    header = make_header(len(aligned), image_crc32, app_base, vector_offset, flags, len(payload), encoded_size, cpu_hz)
    return header + payload


def package_info(package: bytes) -> tuple[bool, int, int, int, int]:
    header = package[:HEADER_SIZE]
    fields = struct.unpack("<12I", header[:48])
    image_size = fields[4]
    flags = fields[8]
    payload_size = fields[9]
    encoded_size = fields[10]
    cpu_hz = fields[11]
    compressed = (flags & IMAGE_FLAG_COMPRESSED_ZLIB) != 0
    return compressed, image_size, payload_size, encoded_size, cpu_hz


def packet_to_symbols(packet: bytes) -> list[int]:
    crc = zlib.crc32(packet) & 0xFFFFFFFF
    payload = (b"\x55" * 4) + packet + struct.pack(">I", crc)
    symbols: list[int] = []
    for byte in payload:
        for bit in range(7, -1, -1):
            symbols.append(1 if (byte >> bit) & 1 else 0)
    return symbols


def encode_symbols(symbols: list[int], zero_period: int, one_period: int, blank_period: int) -> array.array:
    state = 1.0
    output = array.array("h")
    durations = {
        0: zero_period,
        1: one_period,
        2: blank_period,
    }
    for symbol in symbols:
        duration = durations[symbol]
        amplitude = 0.78
        value = int(max(-32767, min(32767, state * amplitude * 32767)))
        output.extend([value] * duration)
        state *= -1.0
    return output


def blank_symbols(seconds: float, sample_rate: int, blank_period: int) -> list[int]:
    count = int(seconds * sample_rate / blank_period) + 1
    return [2] * count


def emit_wav(package: bytes, output_path: Path, args: argparse.Namespace) -> None:
    if len(package) < PACKET_SIZE or (len(package) % PACKET_SIZE) != 0:
        raise ValueError("Packaged image must be packet aligned and include the image header packet")

    waveform = array.array("h")
    symbol_stream: list[int] = []
    waveform.extend([0] * int(args.sample_rate * INTRO_SILENCE_SECONDS))
    symbol_stream.extend(blank_symbols(INTRO_BLANK_SECONDS, args.sample_rate, args.blank_period))

    header_packet = package[:PACKET_SIZE]
    symbol_stream.extend(packet_to_symbols(header_packet))

    image_packet_count = 0
    for offset in range(PACKET_SIZE, len(package), PACKET_SIZE):
        packet = package[offset: offset + PACKET_SIZE]
        symbol_stream.extend(packet_to_symbols(packet))
        image_packet_count += 1
        if len(package) > (PACKET_SIZE + BUFFERED_PAYLOAD_LIMIT) and image_packet_count % (PAGE_GROUP_BYTES // PACKET_SIZE) == 0:
            symbol_stream.extend(blank_symbols(args.group_blank, args.sample_rate, args.blank_period))

    symbol_stream.extend(blank_symbols(OUTRO_BLANK_SECONDS, args.sample_rate, args.blank_period))
    waveform.extend(encode_symbols(symbol_stream, args.zero_period, args.one_period, args.blank_period))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(args.sample_rate)
        wav.writeframes(waveform.tobytes())


def main() -> int:
    args = parse_args()
    args.sketch = args.sketch.expanduser().resolve()
    if args.output is not None:
        args.output = args.output.expanduser().resolve()
    output = args.output or args.sketch.with_suffix(".wav")
    args.resolved_fqbn, args.resolved_cpu_hz = resolve_fqbn_and_cpu_hz(args.fqbn, args.cpu_hz)

    temp_dir_obj = None
    if args.build_dir is not None:
        build_dir = args.build_dir
        build_dir.mkdir(parents=True, exist_ok=True)
    elif args.keep_build_dir:
        build_dir = Path(tempfile.mkdtemp(prefix="audio-bootloader-build-"))
    else:
        temp_dir_obj = tempfile.TemporaryDirectory(prefix="audio-bootloader-build-")
        build_dir = Path(temp_dir_obj.name)

    try:
        binary_path = compile_sketch(args, build_dir)
        raw_image = binary_path.read_bytes()
        package = package_image(raw_image, args.app_base, args.vector_offset, args.compression, args.resolved_cpu_hz)
        compressed, image_size, payload_size, encoded_size, cpu_hz = package_info(package)
        emit_wav(package, output, args)
        print(f"WAV written to {output}")
        print(f"Build directory: {build_dir}")
        print(f"FQBN: {args.resolved_fqbn}")
        print(f"CPU Hz: {cpu_hz}")
        mode = "zlib" if compressed else "none"
        print(f"Compression: {mode}")
        print(f"Image bytes: {image_size}, payload bytes: {payload_size}, encoded bytes: {encoded_size}")
        return 0
    finally:
        if temp_dir_obj is not None and not args.keep_build_dir:
            temp_dir_obj.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
