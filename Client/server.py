#!/usr/bin/env python3
# Copyright 2026 Wenhao Yang
#
# Author: Wenhao Yang
# Contributor: Wenhao Yang
#
# Local HTTP client/server bridge for Pico-Eurorack bootloader UF2 generation.

from __future__ import annotations

import importlib.util
import json
import mimetypes
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
CLIENT_DIR = ROOT / "Client"
BOOT_TOOL = ROOT / "Bootloader" / "Tools" / "pico_boot_apps.py"
SELECTOR_UF2 = ROOT / "Bootloader" / "build" / "Pico_Firmware_selector_only.uf2"
BUILD_ROOT = ROOT / "Bootloader" / "build" / "client"
SLOT_BYTES = 512 * 1024
MAX_SLOTS = 6
DEFAULT_TARGET = "rp2350"
DEFAULT_CPU_HZ = 250_000_000
DEFAULT_FQBN = "rp2040:rp2040:rpipico2:flash=4194304_0,arch=arm,freq=250"


def load_boot_tool():
    spec = importlib.util.spec_from_file_location("pico_boot_apps", BOOT_TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {BOOT_TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


BOOT = load_boot_tool()


@dataclass(frozen=True)
class AppDef:
    id: str
    device: str
    name: str
    sketch: str
    prebuilt: str | None

    @property
    def sketch_path(self) -> Path:
        return ROOT / self.sketch

    @property
    def prebuilt_path(self) -> Path | None:
        return ROOT / self.prebuilt if self.prebuilt else None


APPS = [
    AppDef("TripleOSC", "pico", "TripleOSC", "Sketches/Pico/TripleOSC", "Build/Pico/150MHz/TripleOSC_150MHz.uf2"),
    AppDef("Plaits", "pico", "Plaits", "Sketches/Pico/Plaits", "Build/Pico/250MHz/Plaits_250MHz.uf2"),
    AppDef("PlaitsFM", "pico", "PlaitsFM", "Sketches/Pico/PlaitsFM", "Build/Pico/250MHz/PlaitsFM_250MHz.uf2"),
    AppDef("Rings", "pico", "Rings", "Sketches/Pico/Rings", "Build/Pico/250MHz/Rings_250MHz.uf2"),
    AppDef("Braids", "pico", "Braids", "Sketches/Pico/Braids", "Build/Pico/150MHz/Braids_150MHz.uf2"),
    AppDef("Moogvoice", "pico", "Moogvoice", "Sketches/Pico/Moogvoice", "Build/Pico/200MHz/Moogvoice_200MHz.uf2"),
    AppDef("DRUMS", "pico", "DRUMS", "Sketches/Pico/DRUMS", "Build/Pico/150MHz/DRUMS_150MHz.uf2"),
    AppDef("Branches", "pico", "Branches", "Sketches/Pico/Branches", "Build/Pico/150MHz/Branches_150MHz.uf2"),
    AppDef("DualClock", "pico", "DualClock", "Sketches/Pico/DualClock", "Build/Pico/150MHz/DualClock_150MHz.uf2"),
    AppDef("16Step_Sequencer", "pico", "16Step Sequencer", "Sketches/Pico/16Step_Sequencer", "Build/Pico/150MHz/16Step_Sequencer_150MHz.uf2"),
    AppDef("Motion_Recorder", "pico", "Motion Recorder", "Sketches/Pico/Motion_Recorder", "Build/Pico/150MHz/Motion_Recorder_150MHz.uf2"),
    AppDef("Grids_Sampler", "pico", "Grids Sampler", "Sketches/Pico/Grids_Sampler", "Build/Pico/150MHz/Grids_Sampler_150MHz.uf2"),
    AppDef("DejaVu", "pico", "DejaVu", "Sketches/Pico/DejaVu", "Build/Pico/150MHz/DejaVu_150MHz.uf2"),
    AppDef("Modulation", "pico", "Modulation", "Sketches/Pico/Modulation", "Build/Pico/150MHz/Modulation_150MHz.uf2"),
    AppDef("Oneshot_Sampler", "pico", "Oneshot Sampler", "Sketches/Pico/Oneshot_Sampler", "Build/Pico/150MHz/Oneshot_Sampler.ino.uf2"),
    AppDef("DaisySP_Delay", "picofx", "DaisySP Delay", "Sketches/PicoFX/DaisySP_Delay", "Build/PicoFX/150MHz/DaisySP_Delay_150MHz.uf2"),
    AppDef("DaisySP_Reverb", "picofx", "DaisySP Reverb", "Sketches/PicoFX/DaisySP_Reverb", "Build/PicoFX/150MHz/DaisySP_Reverb_150MHz.uf2"),
    AppDef("DaisySP_Chorus", "picofx", "DaisySP Chorus", "Sketches/PicoFX/DaisySP_Chorus", "Build/PicoFX/150MHz/DaisySP_Chorus_150MHz.uf2"),
    AppDef("DaisySP_Flanger", "picofx", "DaisySP Flanger", "Sketches/PicoFX/DaisySP_Flanger", "Build/PicoFX/250MHz/DaisySP_Flanger_250MHz.uf2"),
    AppDef("DaisySP_Pitchshifter", "picofx", "DaisySP Pitchshifter", "Sketches/PicoFX/DaisySP_Pitchshifter", "Build/PicoFX/150MHz/DaisySP_Pitchshifter_150MHz.uf2"),
    AppDef("Ladderfilter", "picofx", "Ladderfilter", "Sketches/PicoFX/Ladderfilter", "Build/PicoFX/150MHz/Ladderfilter_150MHz.uf2"),
    AppDef("Bitcrush", "picofx", "Bitcrush", "Sketches/PicoFX/Bitcrush", None),
    AppDef("Granular", "picofx", "Granular", "Sketches/PicoFX/Granular", "Build/PicoFX/250MHz/Granular_250MHz.uf2"),
    AppDef("BeatBreaker", "picofx", "BeatBreaker", "Sketches/PicoFX/BeatBreaker", "Build/PicoFX/250MHz/BeatBreaker_250MHz.uf2"),
    AppDef("Sidechain", "picofx", "Sidechain", "Sketches/PicoFX/Sidechain", "Build/PicoFX/150MHz/Sidechain_150MHz.uf2"),
    AppDef("Panner", "picofx", "Panner", "Sketches/PicoFX/Panner", "Build/PicoFX/150MHz/Panner_150MHz.uf2"),
    AppDef("Spectral_Smash", "picofx", "Spectral Smash", "Sketches/PicoFX/Spectral_Smash", "Build/PicoFX/250MHz/Spectral_Smash_250MHz.uf2"),
]

APP_BY_ID = {app.id: app for app in APPS}


def app_size(app: AppDef) -> dict:
    path = app.prebuilt_path if app.prebuilt_path and app.prebuilt_path.exists() else size_probe_uf2(app)
    if path is None or not path.exists():
        return {
            "sizeBytes": None,
            "slotBytes": SLOT_BYTES,
            "fitsSlot": None,
            "source": None,
        }
    blocks = BOOT.read_uf2(path)
    size_bytes = BOOT.slot_usage_bytes(blocks, SLOT_BYTES)
    return {
        "sizeBytes": size_bytes,
        "slotBytes": SLOT_BYTES,
        "fitsSlot": size_bytes <= SLOT_BYTES,
        "source": str(path.relative_to(ROOT)),
    }


def size_probe_uf2(app: AppDef) -> Path | None:
    build_path = BUILD_ROOT / "sizes" / f"slot0-{slug(app.id)}"
    cached = sorted(build_path.glob("*.ino.uf2"))
    if cached:
        return cached[0]
    try:
        return build_slot_to_path(0, app, build_path)
    except Exception:
        return None


def manifest_payload() -> dict:
    apps = []
    for app in APPS:
        row = {
            "id": app.id,
            "device": app.device,
            "name": app.name,
            "sketch": app.sketch,
        }
        row.update(app_size(app))
        apps.append(row)
    return {
        "slotBytes": SLOT_BYTES,
        "maxSlots": MAX_SLOTS,
        "selector": str(SELECTOR_UF2.relative_to(ROOT)),
        "target": DEFAULT_TARGET,
        "cpuHz": DEFAULT_CPU_HZ,
        "apps": apps,
    }


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_") or "app"


def existing_libraries() -> list[Path]:
    candidates = [
        ROOT / "Sketches" / "lib",
        Path.home() / "Documents" / "Arduino" / "libraries",
    ]
    return [path for path in candidates if path.exists()]


def run_command(cmd: list[str]) -> None:
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if proc.returncode:
        tail = "\n".join((proc.stdout + "\n" + proc.stderr).splitlines()[-80:])
        raise RuntimeError(tail or f"command failed: {' '.join(cmd)}")


def build_slot_to_path(slot_index: int, app: AppDef, build_path: Path) -> Path:
    if not app.sketch_path.exists():
        raise RuntimeError(f"missing sketch: {app.sketch}")

    cmd = [
        sys.executable,
        str(BOOT_TOOL),
        "slot-build",
        str(app.sketch_path),
        "--slot",
        str(slot_index),
        "--fqbn",
        DEFAULT_FQBN,
        "--build-path",
        str(build_path),
    ]
    for library in existing_libraries():
        cmd.extend(["--library", str(library)])
    run_command(cmd)

    uf2_files = sorted(build_path.glob("*.ino.uf2"))
    if not uf2_files:
        raise RuntimeError(f"slot build did not produce a UF2 in {build_path}")
    return uf2_files[0]


def build_slot(slot_index: int, app: AppDef) -> Path:
    build_path = BUILD_ROOT / f"slot{slot_index}-{slug(app.id)}"
    return build_slot_to_path(slot_index, app, build_path)


def generate_firmware(request: dict) -> tuple[Path, str]:
    device = request.get("device")
    slots = request.get("slots")
    active = int(request.get("active", 0))
    if device not in {"pico", "picofx"}:
        raise RuntimeError("invalid device")
    if not isinstance(slots, list) or not 1 <= len(slots) <= MAX_SLOTS:
        raise RuntimeError(f"slots must contain 1 to {MAX_SLOTS} entries")
    if not SELECTOR_UF2.exists():
        raise RuntimeError(f"missing selector UF2: {SELECTOR_UF2.relative_to(ROOT)}")

    selected: list[tuple[int, AppDef]] = []
    for index, app_id in enumerate(slots):
        if app_id in (None, ""):
            continue
        app = APP_BY_ID.get(str(app_id))
        if app is None:
            raise RuntimeError(f"unknown app id: {app_id}")
        if app.device != device:
            raise RuntimeError(f"{app.name} does not belong to {device}")
        size = app_size(app)
        if size["fitsSlot"] is False:
            raise RuntimeError(f"{app.name} uses {size['sizeBytes']} bytes and exceeds one 512KB slot")
        selected.append((index, app))

    if not selected:
        raise RuntimeError("select at least one app before generating")

    valid_slots = {index for index, _app in selected}
    if active not in valid_slots:
        active = selected[0][0]

    built = [(index, build_slot(index, app)) for index, app in selected]
    for index, uf2_path in built:
        size_bytes = BOOT.slot_usage_bytes(BOOT.read_uf2(uf2_path), SLOT_BYTES)
        if size_bytes > SLOT_BYTES:
            raise RuntimeError(f"slot {index + 1} app exceeds 512KB")

    timestamp = time.strftime("%Y%m%d-%H%M%S")
    output_name = f"Pico-Eurorack-{device}-{timestamp}.uf2"
    output_path = BUILD_ROOT / "output" / output_name
    cmd = [
        sys.executable,
        str(BOOT_TOOL),
        "package",
        "--selector",
        str(SELECTOR_UF2),
        "--output",
        str(output_path),
        "--target",
        DEFAULT_TARGET,
        "--cpu-hz",
        str(DEFAULT_CPU_HZ),
        "--active",
        str(active),
    ]
    for index, uf2_path in built:
        cmd.extend(["--slot-app", f"{index}={uf2_path}"])
    run_command(cmd)
    return output_path, output_name


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(CLIENT_DIR), **kwargs)

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path.startswith("/images/"):
            self.send_repo_file(ROOT / parsed.path.lstrip("/"))
            return
        if parsed.path == "/api/manifest":
            self.send_json(HTTPStatus.OK, manifest_payload())
            return
        if parsed.path == "/api/health":
            self.send_json(HTTPStatus.OK, {"ok": True})
            return
        super().do_GET()

    def do_HEAD(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path.startswith("/images/"):
            self.send_repo_file(ROOT / parsed.path.lstrip("/"), head_only=True)
            return
        super().do_HEAD()

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path != "/api/generate":
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length) or b"{}")
            output_path, output_name = generate_firmware(payload)
            data = output_path.read_bytes()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Content-Disposition", f'attachment; filename="{output_name}"')
            self.end_headers()
            self.wfile.write(data)
        except Exception as exc:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})

    def send_json(self, status: HTTPStatus, payload: dict) -> None:
        data = json.dumps(payload, indent=2).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_repo_file(self, path: Path, head_only: bool = False) -> None:
        try:
            resolved = path.resolve()
            resolved.relative_to(ROOT)
        except ValueError:
            self.send_error(HTTPStatus.FORBIDDEN)
            return
        if not resolved.is_file():
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        data = resolved.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", self.guess_type(str(resolved)))
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if not head_only:
            self.wfile.write(data)

    def guess_type(self, path: str) -> str:
        if path.endswith(".uf2"):
            return "application/octet-stream"
        return mimetypes.guess_type(path)[0] or super().guess_type(path)


def main() -> None:
    port = 8765
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    for candidate in range(port, port + 20):
        try:
            server = ThreadingHTTPServer(("127.0.0.1", candidate), Handler)
            break
        except OSError:
            server = None
    if server is None:
        raise RuntimeError(f"no available local port from {port} to {port + 19}")
    port = server.server_address[1]
    print(f"Pico-Eurorack Client running at http://127.0.0.1:{port}/")
    server.serve_forever()


if __name__ == "__main__":
    main()
