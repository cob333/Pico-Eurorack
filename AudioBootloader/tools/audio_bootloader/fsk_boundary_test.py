#!/usr/bin/env python3

from __future__ import annotations

import argparse
import random
import tempfile
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace
import zlib

import make_audio_wav as wavtool


PREAMBLE_BITS = 32
MAX_BLANK_SYMBOLS = 500


@dataclass(frozen=True)
class Profile:
    name: str
    zero_period: int
    one_period: int
    blank_period: int

    @property
    def one_threshold(self) -> int:
        return (self.zero_period + self.one_period) // 2

    @property
    def blank_threshold(self) -> int:
        return (self.one_period + self.blank_period) // 2


@dataclass(frozen=True)
class Scenario:
    name: str
    skew: float
    jitter: float
    trials: int


class PyDemodulator:
    def __init__(self, blank_threshold: int, one_threshold: int) -> None:
        self.blank_threshold = blank_threshold
        self.one_threshold = one_threshold
        self.reset()

    def reset(self) -> None:
        self.swallow = 4

    def classify_duration(self, duration: int) -> int:
        if duration >= self.blank_threshold:
            symbol = 2
        elif duration >= self.one_threshold:
            symbol = 1
        else:
            symbol = 0

        if self.swallow > 0:
            self.swallow -= 1
            return 2
        return symbol


class PyPacketDecoder:
    def __init__(self) -> None:
        self.packet_count = 0
        self.reset()

    def reset(self) -> None:
        self.state = "syncing"
        self.expected_symbols = 0xFF
        self.preamble_remaining = PREAMBLE_BITS
        self.sync_blank_size = 0
        self.symbol_count = 0
        self.packet_size = 0
        self.packet = bytearray(wavtool.PACKET_SIZE + 4)

    def process_symbol(self, symbol: int) -> tuple[str, bytes | None]:
        if self.state == "syncing":
            self._parse_sync(symbol)
            return self.state, None

        if self.state == "decoding":
            if symbol == 2:
                self.state = "error_sync"
                return self.state, None
            self.packet[self.packet_size] |= symbol
            self.symbol_count += 1
            if self.symbol_count == 8:
                self.symbol_count = 0
                self.packet_size += 1
                if self.packet_size == wavtool.PACKET_SIZE + 4:
                    actual_crc = zlib.crc32(self.packet[:wavtool.PACKET_SIZE]) & 0xFFFFFFFF
                    expected_crc = int.from_bytes(self.packet[wavtool.PACKET_SIZE:wavtool.PACKET_SIZE + 4], "big")
                    self.packet_count += 1
                    self.state = "ok" if actual_crc == expected_crc else "error_crc"
                    return self.state, bytes(self.packet[:wavtool.PACKET_SIZE])
                self.packet[self.packet_size] = 0
            else:
                self.packet[self.packet_size] <<= 1
            return self.state, None

        return self.state, None

    def _parse_sync(self, symbol: int) -> None:
        if ((1 << symbol) & self.expected_symbols) == 0:
            self.state = "error_sync"
            return

        if symbol == 2:
            self.sync_blank_size += 1
            if self.sync_blank_size >= MAX_BLANK_SYMBOLS and self.packet_count > 0:
                self.state = "eot"
                return
            self.expected_symbols = (1 << 0) | (1 << 1) | (1 << 2)
            self.preamble_remaining = PREAMBLE_BITS
        elif symbol == 1:
            self.expected_symbols = 1 << 0
            if self.preamble_remaining > 0:
                self.preamble_remaining -= 1
        else:
            self.expected_symbols = 1 << 1
            if self.preamble_remaining > 0:
                self.preamble_remaining -= 1

        if self.preamble_remaining == 0:
            self.state = "decoding"
            self.packet_size = 0
            self.symbol_count = 0
            self.packet = bytearray(wavtool.PACKET_SIZE + 4)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run an offline FSK timing margin sweep against the current audio bootloader protocol")
    parser.add_argument("sketch", type=Path, help="Path to the .ino sketch to package")
    parser.add_argument("--compression", choices=("auto", "off", "zlib"), default="auto")
    parser.add_argument("--fqbn", default="rp2040:rp2040:rpipico2")
    parser.add_argument("--arduino-cli", default="arduino-cli")
    parser.add_argument("--suite", choices=("quick", "stress"), default="stress")
    parser.add_argument("--trials", type=int, default=6, help="Random trials per jitter scenario")
    return parser.parse_args()


def build_symbol_stream(package: bytes, profile: Profile) -> list[int]:
    symbols: list[int] = []
    symbols.extend(wavtool.blank_symbols(wavtool.INTRO_BLANK_SECONDS, wavtool.SAMPLE_RATE, profile.blank_period))
    header_packet = package[:wavtool.PACKET_SIZE]
    symbols.extend(wavtool.packet_to_symbols(header_packet))

    image_packet_count = 0
    for offset in range(wavtool.PACKET_SIZE, len(package), wavtool.PACKET_SIZE):
        symbols.extend(wavtool.packet_to_symbols(package[offset: offset + wavtool.PACKET_SIZE]))
        image_packet_count += 1
        if len(package) > (wavtool.PACKET_SIZE + wavtool.BUFFERED_PAYLOAD_LIMIT):
            packets_per_group = wavtool.PAGE_GROUP_BYTES // wavtool.PACKET_SIZE
            if image_packet_count % packets_per_group == 0:
                symbols.extend(wavtool.blank_symbols(wavtool.GROUP_BLANK_SECONDS, wavtool.SAMPLE_RATE, profile.blank_period))

    symbols.extend(wavtool.blank_symbols(wavtool.OUTRO_BLANK_SECONDS, wavtool.SAMPLE_RATE, profile.blank_period))
    return symbols


def expected_packets(package: bytes) -> list[bytes]:
    return [package[offset: offset + wavtool.PACKET_SIZE] for offset in range(0, len(package), wavtool.PACKET_SIZE)]


def profile_duration_seconds(symbols: list[int], profile: Profile) -> float:
    total_samples = int(wavtool.SAMPLE_RATE * wavtool.INTRO_SILENCE_SECONDS)
    for symbol in symbols:
        if symbol == 0:
            total_samples += profile.zero_period
        elif symbol == 1:
            total_samples += profile.one_period
        else:
            total_samples += profile.blank_period
    return total_samples / wavtool.SAMPLE_RATE


def simulate(symbols: list[int], packets: list[bytes], profile: Profile, skew: float, jitter: float, seed: int) -> tuple[bool, str]:
    rng = random.Random(seed)
    demod = PyDemodulator(profile.blank_threshold, profile.one_threshold)
    decoder = PyPacketDecoder()
    packet_index = 0
    header_received = False

    periods = {
        0: profile.zero_period,
        1: profile.one_period,
        2: profile.blank_period,
    }

    for tx_symbol in symbols:
        ideal = periods[tx_symbol]
        measured = int(round(((ideal - 1) * skew) + rng.uniform(-jitter, jitter)))
        measured = max(0, measured)
        rx_symbol = demod.classify_duration(measured)
        state, packet = decoder.process_symbol(rx_symbol)

        if state == "ok":
            if packet is None or packet_index >= len(packets):
                return False, "unexpected-packet"
            if packet != packets[packet_index]:
                return False, "packet-mismatch"
            packet_index += 1
            header_received = True
            decoder.reset()
        elif state == "eot":
            if packet_index == len(packets):
                return True, "eot"
            return False, f"eot-{packet_index}/{len(packets)}"
        elif state in ("error_sync", "error_crc"):
            if header_received:
                return False, state
            demod.reset()
            decoder.reset()

    return False, "no-eot"


def compile_package(args: argparse.Namespace) -> bytes:
    build_dir = Path(tempfile.mkdtemp(prefix="audio-boot-boundary-"))
    resolved_fqbn, resolved_cpu_hz = wavtool.resolve_fqbn_and_cpu_hz(args.fqbn, None)
    compile_args = SimpleNamespace(
        sketch=args.sketch,
        fqbn=args.fqbn,
        resolved_fqbn=resolved_fqbn,
        resolved_cpu_hz=resolved_cpu_hz,
        output=None,
        build_dir=build_dir,
        keep_build_dir=True,
        arduino_cli=args.arduino_cli,
        sample_rate=wavtool.SAMPLE_RATE,
        zero_period=wavtool.ZERO_PERIOD,
        one_period=wavtool.ONE_PERIOD,
        blank_period=wavtool.BLANK_PERIOD,
        group_blank=wavtool.GROUP_BLANK_SECONDS,
        compression=args.compression,
        app_base=wavtool.APP_BASE,
        slot_size=wavtool.APP_SLOT_SIZE,
        vector_offset=wavtool.APP_VECTOR_OFFSET,
    )
    binary_path = wavtool.compile_sketch(compile_args, build_dir)
    raw_image = binary_path.read_bytes()
    return wavtool.package_image(raw_image, wavtool.APP_BASE, wavtool.APP_VECTOR_OFFSET, args.compression, resolved_cpu_hz)


def main() -> int:
    args = parse_args()
    package = compile_package(args)
    packets = expected_packets(package)

    profiles = [
        Profile("baseline", 12, 24, 72),
        Profile("fast-11", 11, 22, 66),
        Profile("fast-10", 10, 20, 60),
        Profile("aggressive-9", 9, 18, 54),
        Profile("aggressive-8", 8, 16, 48),
    ]
    if args.suite == "quick":
        scenarios = [
            Scenario("exact", 1.000, 0.0, 1),
            Scenario("drift-1%", 0.990, 0.0, 1),
            Scenario("drift+1%", 1.010, 0.0, 1),
            Scenario("jitter-1", 1.000, 1.0, args.trials),
            Scenario("jitter-2", 1.000, 2.0, args.trials),
            Scenario("drift-1%+j1", 0.990, 1.0, args.trials),
            Scenario("drift+1%+j1", 1.010, 1.0, args.trials),
        ]
    else:
        scenarios = [
            Scenario("drift-2%", 0.980, 0.0, 1),
            Scenario("drift+2%", 1.020, 0.0, 1),
            Scenario("jitter-3", 1.000, 3.0, args.trials),
            Scenario("jitter-4", 1.000, 4.0, args.trials),
            Scenario("drift-2%+j2", 0.980, 2.0, args.trials),
            Scenario("drift+2%+j2", 1.020, 2.0, args.trials),
        ]

    print(f"Sketch: {args.sketch}")
    print(f"Compression: {args.compression}")
    print(f"Suite: {args.suite}")
    print(f"Package bytes: {len(package)} ({len(packets)} packets)")
    print()

    for profile in profiles:
        symbols = build_symbol_stream(package, profile)
        duration = profile_duration_seconds(symbols, profile)
        scenario_results: list[str] = []
        all_pass = True
        for scenario in scenarios:
            passes = 0
            fail_reason = ""
            for trial in range(scenario.trials):
                ok, reason = simulate(
                    symbols,
                    packets,
                    profile,
                    skew=scenario.skew,
                    jitter=scenario.jitter,
                    seed=(trial + 1) * 7919 + profile.zero_period * 101,
                )
                if ok:
                    passes += 1
                else:
                    fail_reason = reason
            if passes == scenario.trials:
                scenario_results.append(f"{scenario.name}=PASS")
            else:
                all_pass = False
                scenario_results.append(f"{scenario.name}={passes}/{scenario.trials} ({fail_reason})")

        verdict = "PASS" if all_pass else "FAIL"
        print(
            f"{profile.name:>12}  {profile.zero_period:>2}/{profile.one_period:>2}/{profile.blank_period:<2}  "
            f"{duration:7.3f}s  {verdict}  " + "  ".join(scenario_results)
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
