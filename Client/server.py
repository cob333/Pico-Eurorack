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
import threading
import time
import uuid
from dataclasses import dataclass
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse


ROOT = Path(__file__).resolve().parents[1]
CLIENT_DIR = ROOT / "Client"
BOOT_TOOL = ROOT / "Bootloader" / "Tools" / "pico_boot_apps.py"
SELECTOR_UF2 = ROOT / "Bootloader" / "build" / "Pico_Firmware_selector_only.uf2"
BUILD_ROOT = ROOT / "Bootloader" / "build" / "client"
APP_REGION_BYTES = 3584 * 1024
SLOT_ALIGN_BYTES = 4096
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
JOBS: dict[str, "BuildJob"] = {}
JOBS_LOCK = threading.Lock()


class BuildCancelled(RuntimeError):
    pass


@dataclass
class BuildJob:
    id: str
    status: str = "queued"
    progress: int = 0
    message: str = "Queued"
    output_path: Path | None = None
    output_name: str | None = None
    error: str | None = None
    process: subprocess.Popen | None = None
    cancel_requested: bool = False

    def snapshot(self) -> dict:
        return {
            "id": self.id,
            "status": self.status,
            "progress": self.progress,
            "message": self.message,
            "outputName": self.output_name,
            "error": self.error,
        }

    def update(self, progress: int, message: str) -> None:
        self.progress = max(0, min(100, int(progress)))
        self.message = message

    def cancel(self) -> None:
        self.cancel_requested = True
        self.status = "cancelled"
        self.message = "Cancelled"
        if self.process and self.process.poll() is None:
            self.process.terminate()


def align_up(value: int, alignment: int) -> int:
    return ((value + alignment - 1) // alignment) * alignment


def app_size(app: AppDef) -> dict:
    path = app.prebuilt_path if app.prebuilt_path and app.prebuilt_path.exists() else size_probe_uf2(app)
    if path is None or not path.exists():
        return {
            "sizeBytes": None,
            "allocatedBytes": None,
            "fitsRegion": None,
            "source": None,
        }
    blocks = BOOT.read_uf2(path)
    size_bytes = BOOT.app_usage_bytes(blocks)
    allocated_bytes = align_up(size_bytes, SLOT_ALIGN_BYTES)
    return {
        "sizeBytes": size_bytes,
        "allocatedBytes": allocated_bytes,
        "fitsRegion": allocated_bytes <= APP_REGION_BYTES,
        "source": str(path.relative_to(ROOT)),
    }


def size_probe_uf2(app: AppDef) -> Path | None:
    build_path = BUILD_ROOT / "sizes" / f"slot0-{slug(app.id)}"
    cached = sorted(build_path.glob("*.ino.uf2"))
    if cached:
        return cached[0]
    try:
        return build_slot_to_path(
            0,
            app,
            build_path,
            BOOT.PICO_SELECTOR_FIRST_APP_OFFSET,
            APP_REGION_BYTES,
        )
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
        "storageBytes": APP_REGION_BYTES,
        "slotAlignBytes": SLOT_ALIGN_BYTES,
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


def run_command(cmd: list[str], job: BuildJob | None = None) -> None:
    if job and job.cancel_requested:
        raise BuildCancelled("cancelled")
    proc = subprocess.Popen(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if job:
        job.process = proc
    stdout, stderr = proc.communicate()
    if job:
        job.process = None
    if job and job.cancel_requested:
        raise BuildCancelled("cancelled")
    if proc.returncode:
        tail = "\n".join((stdout + "\n" + stderr).splitlines()[-80:])
        raise RuntimeError(tail or f"command failed: {' '.join(cmd)}")


def build_slot_to_path(
    slot_index: int,
    app: AppDef,
    build_path: Path,
    slot_offset: int,
    slot_size: int,
    job: BuildJob | None = None,
) -> Path:
    if not app.sketch_path.exists():
        raise RuntimeError(f"missing sketch: {app.sketch}")

    cmd = [
        sys.executable,
        str(BOOT_TOOL),
        "slot-build",
        str(app.sketch_path),
        "--slot",
        str(slot_index),
        "--slot-offset",
        str(slot_offset),
        "--slot-size",
        str(slot_size),
        "--fqbn",
        DEFAULT_FQBN,
        "--build-path",
        str(build_path),
    ]
    for library in existing_libraries():
        cmd.extend(["--library", str(library)])
    run_command(cmd, job)

    uf2_files = sorted(build_path.glob("*.ino.uf2"))
    if not uf2_files:
        raise RuntimeError(f"slot build did not produce a UF2 in {build_path}")
    return uf2_files[0]


def build_slot(slot_index: int, app: AppDef, slot_offset: int, slot_size: int, job: BuildJob | None = None) -> Path:
    build_path = BUILD_ROOT / f"slot{slot_index}-{slug(app.id)}"
    return build_slot_to_path(slot_index, app, build_path, slot_offset, slot_size, job)


def plan_layout(selected: list[tuple[int, AppDef]]) -> list[dict]:
    next_offset = BOOT.PICO_SELECTOR_FIRST_APP_OFFSET
    region_end = BOOT.PICO_SELECTOR_FIRST_APP_OFFSET + APP_REGION_BYTES
    plan = []
    for slot_index, app in selected:
        size = app_size(app)
        if not isinstance(size["sizeBytes"], int):
            raise RuntimeError(f"cannot measure {app.name}")
        allocated = align_up(size["sizeBytes"], SLOT_ALIGN_BYTES)
        if next_offset + allocated > region_end:
            raise RuntimeError(
                f"selected apps use {next_offset + allocated - BOOT.PICO_SELECTOR_FIRST_APP_OFFSET} bytes, "
                f"exceeding the 3.5MiB app region"
            )
        plan.append(
            {
                "slot": slot_index,
                "app": app,
                "offset": next_offset,
                "size": allocated,
                "used": size["sizeBytes"],
            }
        )
        next_offset += allocated
    return plan


def generate_firmware(request: dict, job: BuildJob | None = None) -> tuple[Path, str]:
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
        selected.append((index, app))

    if not selected:
        raise RuntimeError("select at least one app before generating")

    valid_slots = {index for index, _app in selected}
    if active not in valid_slots:
        active = selected[0][0]

    layout = plan_layout(selected)
    if job:
        job.status = "running"
        job.update(3, "Planning")
    built = []
    total_steps = len(layout) + 1
    for step_index, item in enumerate(layout):
        if job and job.cancel_requested:
            raise BuildCancelled("cancelled")
        if job:
            start_percent = 5 + int((step_index / total_steps) * 85)
            job.update(start_percent, f"Building {item['app'].name}")
        uf2_path = build_slot(item["slot"], item["app"], item["offset"], item["size"], job)
        size_bytes = BOOT.app_usage_bytes(BOOT.read_uf2(uf2_path))
        if size_bytes > item["size"]:
            raise RuntimeError(f"slot {item['slot'] + 1} app exceeds its allocated region")
        built.append((item["slot"], uf2_path))
        if job:
            done_percent = 5 + int(((step_index + 1) / total_steps) * 85)
            job.update(done_percent, f"Built {item['app'].name}")

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
        "--app-region-size",
        str(APP_REGION_BYTES),
        "--slot-padding",
        "0",
        "--active",
        str(active),
    ]
    for index, uf2_path in built:
        cmd.extend(["--slot-app", f"{index}={uf2_path}"])
    if job:
        job.update(92, "Packaging UF2")
    run_command(cmd, job)
    if job:
        job.update(100, "Generated")
    return output_path, output_name


def start_generate_job(payload: dict) -> BuildJob:
    job = BuildJob(id=uuid.uuid4().hex)
    with JOBS_LOCK:
        JOBS[job.id] = job

    def worker() -> None:
        try:
            output_path, output_name = generate_firmware(payload, job)
            if job.cancel_requested:
                job.status = "cancelled"
                job.message = "Cancelled"
                return
            job.output_path = output_path
            job.output_name = output_name
            job.status = "done"
            job.update(100, "Generated")
        except BuildCancelled:
            job.status = "cancelled"
            job.message = "Cancelled"
        except Exception as exc:
            job.status = "error"
            job.error = str(exc)
            job.message = "Failed"

    threading.Thread(target=worker, daemon=True).start()
    return job


def get_job(job_id: str | None) -> BuildJob | None:
    if not job_id:
        return None
    with JOBS_LOCK:
        return JOBS.get(job_id)


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
        if parsed.path == "/api/generate/status":
            job_id = parse_qs(parsed.query).get("id", [""])[0]
            job = get_job(job_id)
            if not job:
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "job not found"})
                return
            self.send_json(HTTPStatus.OK, job.snapshot())
            return
        if parsed.path == "/api/generate/download":
            job_id = parse_qs(parsed.query).get("id", [""])[0]
            job = get_job(job_id)
            if not job or job.status != "done" or not job.output_path or not job.output_path.exists():
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "output not found"})
                return
            self.send_download(job.output_path, job.output_name or job.output_path.name)
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
        if parsed.path == "/api/generate/start":
            try:
                length = int(self.headers.get("Content-Length", "0"))
                payload = json.loads(self.rfile.read(length) or b"{}")
                job = start_generate_job(payload)
                self.send_json(HTTPStatus.OK, job.snapshot())
            except Exception as exc:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
            return
        if parsed.path == "/api/generate/cancel":
            try:
                length = int(self.headers.get("Content-Length", "0"))
                payload = json.loads(self.rfile.read(length) or b"{}")
                job = get_job(payload.get("id"))
                if not job:
                    self.send_json(HTTPStatus.NOT_FOUND, {"error": "job not found"})
                    return
                job.cancel()
                self.send_json(HTTPStatus.OK, job.snapshot())
            except Exception as exc:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
            return
        self.send_error(HTTPStatus.NOT_FOUND)

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

    def send_download(self, path: Path, filename: str) -> None:
        data = path.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        self.end_headers()
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
