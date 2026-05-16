#!/usr/bin/env python3
# Copyright 2026 Wenhao Yang
#
# Author: Wenhao Yang
# Contributor: Wenhao Yang
#
# Local HTTP client/server bridge for Pico-Eurorack bootloader UF2 generation.

from __future__ import annotations

import importlib.util
import cgi
import json
import mimetypes
import os
import re
import shutil
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
SELECTOR_UF2_ENV = "PICO_SELECTOR_UF2"
SELECTOR_UF2_CANDIDATES = [
    ROOT / "Bootloader" / "build" / "Pico_Firmware.uf2",
    ROOT / "Bootloader" / "build" / "pico_selector.uf2",
]
BUILD_ROOT = ROOT / "Bootloader" / "build" / "client"
SIZE_CACHE_ROOT = BUILD_ROOT / "sizes"
SAMPLE_DEFAULT_ROOT = BUILD_ROOT / "sample-defaults"
APP_REGION_BYTES = 3584 * 1024
SLOT_ALIGN_BYTES = 4096
MAX_SLOTS = 6
DEFAULT_TARGET = "rp2350"
DEFAULT_CPU_HZ = 250_000_000
DEFAULT_FQBN = "rp2040:rp2040:rpipico2:flash=4194304_0,arch=arm,freq=250"
BUILD_CACHE_VERSION = 1
SKIP_MTIME_DIRS = {".git", "__pycache__"}

LIBRARY_PATHS_CACHE: list[Path] | None = None
UF2_SIZE_CACHE: dict[tuple[str, int, int], dict] = {}
SOURCE_MTIME_CACHE: dict[str, tuple[float, int]] = {}
LIBRARY_MTIME_CACHE: tuple[float, int] | None = None
CACHE_LOCK = threading.Lock()


def load_boot_tool():
    spec = importlib.util.spec_from_file_location("pico_boot_apps", BOOT_TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {BOOT_TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


BOOT = load_boot_tool()


def display_path(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def selector_candidates() -> list[Path]:
    candidates: list[Path] = []
    env_path = os.environ.get(SELECTOR_UF2_ENV)
    if env_path:
        candidates.append(Path(env_path).expanduser())
    candidates.extend(SELECTOR_UF2_CANDIDATES)
    seen: set[Path] = set()
    unique: list[Path] = []
    for path in candidates:
        resolved = path if path.is_absolute() else ROOT / path
        if resolved not in seen:
            seen.add(resolved)
            unique.append(resolved)
    return unique


def build_selector_uf2(job: "BuildJob | None" = None) -> None:
    build_dir = ROOT / "Bootloader" / "build"
    if shutil.which("cmake") is None:
        raise RuntimeError("missing selector UF2 and cmake is not available to build it")

    if not (build_dir / "build.ninja").exists() and not (build_dir / "Makefile").exists():
        if job:
            job.update(4, "Configuring selector")
        configure_cmd = [
            "cmake",
            "-S",
            str(ROOT / "Bootloader" / "Selector"),
            "-B",
            str(build_dir),
            "-DPICO_BOARD=pico2",
        ]
        if shutil.which("ninja"):
            configure_cmd.extend(["-G", "Ninja"])
        run_command(configure_cmd, job)

    if job:
        job.update(4, "Building selector")
    run_command(["cmake", "--build", str(build_dir), "--target", "pico_selector"], job)


def selector_uf2_path(job: "BuildJob | None" = None, *, build_if_missing: bool = False) -> Path:
    for candidate in selector_candidates():
        if candidate.exists():
            return candidate

    if build_if_missing:
        build_selector_uf2(job)
        for candidate in selector_candidates():
            if candidate.exists():
                return candidate

    tried = ", ".join(display_path(path) for path in selector_candidates())
    raise RuntimeError(
        f"missing selector UF2. Tried: {tried}. "
        f"Set {SELECTOR_UF2_ENV} to override the selector UF2 path."
    )


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
    AppDef("Moogvoice", "pico", "Moogvoice", "Sketches/Pico/Moogvoice", "Build/Pico/200MHz/Moogvoice_200MHz.uf2"),
    AppDef("Drums", "pico", "Drums", "Sketches/Pico/Drums", None),
    AppDef("Branches", "pico", "Branches", "Sketches/Pico/Branches", "Build/Pico/150MHz/Branches_150MHz.uf2"),
    AppDef("DualClock", "pico", "DualClock", "Sketches/Pico/DualClock", "Build/Pico/150MHz/DualClock_150MHz.uf2"),
    AppDef("Sequencer", "pico", "Sequencer", "Sketches/Pico/Sequencer", None),
    AppDef("MotionRecorder", "pico", "MotionRecorder", "Sketches/Pico/MotionRecorder", None),
    AppDef("GridsSampler", "pico", "GridsSampler", "Sketches/Pico/GridsSampler", None),
    AppDef("DejaVu", "pico", "DejaVu", "Sketches/Pico/DejaVu", "Build/Pico/150MHz/DejaVu_150MHz.uf2"),
    AppDef("Modulation", "pico", "Modulation", "Sketches/Pico/Modulation", "Build/Pico/150MHz/Modulation_150MHz.uf2"),
    AppDef("OneshotSampler", "pico", "OneshotSampler", "Sketches/Pico/OneshotSampler", None),
    AppDef("Delay", "picofx", "Delay", "Sketches/PicoFX/Delay", None),
    AppDef("Reverb", "picofx", "Reverb", "Sketches/PicoFX/Reverb", None),
    AppDef("Chorus", "picofx", "Chorus", "Sketches/PicoFX/Chorus", None),
    AppDef("Flanger", "picofx", "Flanger", "Sketches/PicoFX/Flanger", None),
    AppDef("Ladderfilter", "picofx", "Ladderfilter", "Sketches/PicoFX/Ladderfilter", "Build/PicoFX/150MHz/Ladderfilter_150MHz.uf2"),
    AppDef("Bitcrush", "picofx", "Bitcrush", "Sketches/PicoFX/Bitcrush", None),
    AppDef("Granular", "picofx", "Granular", "Sketches/PicoFX/Granular", "Build/PicoFX/250MHz/Granular_250MHz.uf2"),
    AppDef("BeatBreaker", "picofx", "BeatBreaker", "Sketches/PicoFX/BeatBreaker", "Build/PicoFX/250MHz/BeatBreaker_250MHz.uf2"),
    AppDef("Sidechain", "picofx", "Sidechain", "Sketches/PicoFX/Sidechain", "Build/PicoFX/150MHz/Sidechain_150MHz.uf2"),
    AppDef("Panner", "picofx", "Panner", "Sketches/PicoFX/Panner", "Build/PicoFX/150MHz/Panner_150MHz.uf2"),
    AppDef("Space", "picofx", "Space", "Sketches/PicoFX/Space", None),
    AppDef("SpectralSmash", "picofx", "SpectralSmash", "Sketches/PicoFX/SpectralSmash", None),
]

APP_BY_ID = {app.id: app for app in APPS}
JOBS: dict[str, "BuildJob"] = {}
JOBS_LOCK = threading.Lock()
SAMPLE_APPS = {
    "GridsSampler": ROOT / "Sketches" / "Pico" / "GridsSampler" / "Samples",
    "OneshotSampler": ROOT / "Sketches" / "Pico" / "OneshotSampler" / "Samples",
}
SAMPLE_LOCK = threading.RLock()


class BuildCancelled(RuntimeError):
    pass


class SizeOverflow(RuntimeError):
    def __init__(self, size_bytes: int):
        super().__init__(f"app exceeds available flash by {size_bytes - APP_REGION_BYTES} bytes")
        self.size_bytes = size_bytes


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


def file_signature(path: Path) -> tuple[str, int, int]:
    stat = path.stat()
    return (str(path), stat.st_mtime_ns, stat.st_size)


def tree_latest_mtime_ns(path: Path) -> int:
    if not path.exists():
        return 0
    latest = path.stat().st_mtime_ns
    if path.is_file():
        return latest
    for root, dirs, files in os.walk(path):
        dirs[:] = [
            name for name in dirs
            if name not in SKIP_MTIME_DIRS and not name.startswith(".") and not name.startswith("build_")
        ]
        root_path = Path(root)
        for name in files:
            if name == ".DS_Store":
                continue
            try:
                latest = max(latest, (root_path / name).stat().st_mtime_ns)
            except OSError:
                pass
    return latest


def build_inputs_mtime_ns(app: AppDef) -> int:
    now = time.monotonic()
    with CACHE_LOCK:
        cached = SOURCE_MTIME_CACHE.get(app.id)
        if cached and now - cached[0] < 2.0:
            return cached[1]

    latest = max(
        tree_latest_mtime_ns(app.sketch_path),
        tree_latest_mtime_ns(BOOT_TOOL),
        tree_latest_mtime_ns(BOOT_TOOL.with_name("make_slot_memmap.py")),
        libraries_mtime_ns(),
    )

    with CACHE_LOCK:
        SOURCE_MTIME_CACHE[app.id] = (now, latest)
    return latest


def libraries_mtime_ns() -> int:
    global LIBRARY_MTIME_CACHE
    now = time.monotonic()
    with CACHE_LOCK:
        if LIBRARY_MTIME_CACHE and now - LIBRARY_MTIME_CACHE[0] < 2.0:
            return LIBRARY_MTIME_CACHE[1]

    latest = 0
    for library in existing_libraries():
        latest = max(latest, tree_latest_mtime_ns(library))

    with CACHE_LOCK:
        LIBRARY_MTIME_CACHE = (now, latest)
    return latest


def read_app_size(path: Path) -> dict:
    key = file_signature(path)
    with CACHE_LOCK:
        cached = UF2_SIZE_CACHE.get(key)
        if cached:
            return dict(cached)

    blocks = BOOT.read_uf2(path)
    size_bytes = BOOT.app_usage_bytes(blocks)
    allocated_bytes = align_up(size_bytes, SLOT_ALIGN_BYTES)
    payload = {
        "sizeBytes": size_bytes,
        "allocatedBytes": allocated_bytes,
        "fitsRegion": allocated_bytes <= APP_REGION_BYTES,
        "source": display_path(path),
    }
    with CACHE_LOCK:
        UF2_SIZE_CACHE[key] = dict(payload)
    return payload


def app_size(app: AppDef, build_missing: bool = False) -> dict:
    try:
        path = size_probe_uf2(app, build_missing=build_missing)
        if path is None and app.prebuilt_path and app.prebuilt_path.exists():
            path = app.prebuilt_path
    except SizeOverflow as exc:
        allocated_bytes = align_up(exc.size_bytes, SLOT_ALIGN_BYTES)
        return {
            "sizeBytes": exc.size_bytes,
            "allocatedBytes": allocated_bytes,
            "fitsRegion": False,
            "source": None,
        }
    if path is None or not path.exists():
        return {
            "sizeBytes": None,
            "allocatedBytes": None,
            "fitsRegion": None,
            "source": None,
        }
    return read_app_size(path)


def size_probe_uf2(app: AppDef, build_missing: bool = False) -> Path | None:
    prune_size_cache_root()
    build_path = size_cache_build_path(app)
    prune_size_cache_build_path(build_path)
    cached = sorted(build_path.glob("*.ino.uf2"))
    if cached:
        return cached[0]
    if not build_missing:
        return None
    try:
        uf2_path = build_slot_to_path(
            0,
            app,
            build_path,
            BOOT.PICO_SELECTOR_FIRST_APP_OFFSET,
            APP_REGION_BYTES,
        )
        prune_size_cache_build_path(build_path)
        prune_size_cache_root()
        return uf2_path
    except Exception as exc:
        match = re.search(r"region `FLASH' overflowed by (\d+) bytes", str(exc))
        if match:
            raise SizeOverflow(APP_REGION_BYTES + int(match.group(1))) from exc
        raise RuntimeError(f"cannot build size probe for {app.name}:\n{exc}") from exc


def size_cache_build_path(app: AppDef) -> Path:
    return SIZE_CACHE_ROOT / f"slot0-{slug(app.id)}"


def prune_size_cache_build_path(build_path: Path) -> None:
    if not build_path.exists():
        return
    for item in build_path.iterdir():
        if item.is_file() and item.suffix == ".uf2":
            continue
        if item.is_dir():
            shutil.rmtree(item)
        else:
            item.unlink(missing_ok=True)


def prune_size_cache_root() -> None:
    if not SIZE_CACHE_ROOT.exists():
        return
    sketches = SIZE_CACHE_ROOT / "_sketches"
    if sketches.exists():
        shutil.rmtree(sketches)
    for item in SIZE_CACHE_ROOT.iterdir():
        if item.is_file() and item.suffix != ".uf2":
            item.unlink(missing_ok=True)


def manifest_payload() -> dict:
    apps = []
    for app in APPS:
        apps.append(app_payload(app, build_missing=False))
    try:
        selector_path = display_path(selector_uf2_path())
    except RuntimeError:
        selector_path = display_path(selector_candidates()[0])
    return {
        "storageBytes": APP_REGION_BYTES,
        "slotAlignBytes": SLOT_ALIGN_BYTES,
        "maxSlots": MAX_SLOTS,
        "selector": selector_path,
        "target": DEFAULT_TARGET,
        "cpuHz": DEFAULT_CPU_HZ,
        "apps": apps,
    }


def app_payload(app: AppDef, build_missing: bool = False) -> dict:
    row = {
        "id": app.id,
        "device": app.device,
        "name": app.name,
        "sketch": app.sketch,
    }
    row.update(app_size(app, build_missing=build_missing))
    return row


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_") or "app"


def safe_segment(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_. -]+", "_", value).strip(" ._-")
    return cleaned or "Samples"


def safe_filename(value: str) -> str:
    name = Path(value).name
    stem = safe_segment(Path(name).stem)
    suffix = Path(name).suffix.lower()
    if suffix != ".wav":
        raise RuntimeError("only .wav files are supported")
    return f"{stem}.wav"


def wav_files(path: Path) -> list[Path]:
    files = {item.resolve(): item for item in list(path.glob("*.wav")) + list(path.glob("*.WAV"))}
    return sorted(files.values(), key=lambda item: item.name.lower())


def sample_file_rows(files: list[Path]) -> tuple[list[dict], int]:
    rows = []
    total = 0
    for item in files:
        size = item.stat().st_size
        total += size
        rows.append({"name": item.name, "bytes": size})
    return rows, total


def sample_bank_entries(app_id: str) -> list[tuple[Path, list[Path]]]:
    root = SAMPLE_APPS.get(app_id)
    if not root or not root.exists():
        raise RuntimeError(f"sample app not supported: {app_id}")
    banks = []
    for path in sorted(root.iterdir()):
        if not path.is_dir() or path.name.startswith("."):
            continue
        wavs = wav_files(path)
        if wavs:
            banks.append((path, wavs))
    return banks


def sample_includes_from_header(sample_root: Path) -> list[dict]:
    manifest = sample_root / "Samples.h"
    if not manifest.exists():
        manifest = sample_root / "samples.h"
    if not manifest.exists():
        return []
    includes = []
    for line in manifest.read_text(errors="replace").splitlines():
        match = re.match(r'\s*#include\s+"([^"]+\.h)"', line)
        if not match:
            continue
        header = match.group(1)
        if header == "sampledefs.h":
            continue
        path = (sample_root / header).resolve()
        item = {"name": header, "bytes": None}
        try:
            path.relative_to(sample_root.resolve())
            if path.exists():
                item["bytes"] = path.stat().st_size
        except ValueError:
            pass
        includes.append(item)
    return includes


def samples_payload(app_id: str) -> dict:
    root = SAMPLE_APPS.get(app_id)
    if not root or not root.exists():
        raise RuntimeError(f"sample app not supported: {app_id}")
    if app_id == "GridsSampler":
        includes = sample_includes_from_header(root)
        wav_rows, _wav_bytes = sample_file_rows(wav_files(root))
        return {
            "app": app_id,
            "root": str(root.relative_to(ROOT)),
            "banks": [],
            "libraryFiles": includes,
            "wavs": wav_rows,
            "bankless": True,
        }

    banks = []
    for bank, wavs in sample_bank_entries(app_id):
        wav_rows, wav_bytes = sample_file_rows(wavs)
        banks.append(
            {
                "name": bank.name,
                "path": str(bank.relative_to(ROOT)),
                "count": len(wav_rows),
                "bytes": wav_bytes,
                "samples": wav_rows,
            }
        )
    return {
        "app": app_id,
        "root": str(root.relative_to(ROOT)),
        "banks": banks,
        "bankless": False,
    }


def sample_copy_ignore(_dir: str, names: list[str]) -> set[str]:
    ignored = {".DS_Store", "__pycache__"}
    ignored.update(name for name in names if name.startswith("build_"))
    return ignored


def sample_default_path(app_id: str) -> Path:
    return SAMPLE_DEFAULT_ROOT / app_id


def sample_tree_signature(path: Path) -> list[tuple[str, int]]:
    if not path.exists():
        return []
    rows = []
    for item in sorted(path.rglob("*")):
        if item.is_dir() or item.name == ".DS_Store" or item.name.startswith("build_"):
            continue
        rows.append((str(item.relative_to(path)), item.stat().st_size))
    return rows


def ensure_sample_defaults() -> None:
    SAMPLE_DEFAULT_ROOT.mkdir(parents=True, exist_ok=True)
    for app_id, source in SAMPLE_APPS.items():
        snapshot = sample_default_path(app_id)
        if snapshot.exists():
            if sample_tree_signature(source) != sample_tree_signature(snapshot):
                restore_sample_default(app_id)
            continue
        if not source.exists():
            continue
        shutil.copytree(source, snapshot, ignore=sample_copy_ignore)


def restore_sample_default(app_id: str) -> bool:
    source = SAMPLE_APPS.get(app_id)
    snapshot = sample_default_path(app_id)
    if not source or not snapshot.exists():
        return False
    if source.exists():
        shutil.rmtree(source)
    shutil.copytree(snapshot, source, ignore=sample_copy_ignore)
    invalidate_app_build_cache(app_id)
    return True


def restore_sample_defaults(app_ids: set[str]) -> None:
    for app_id in sorted(app_ids):
        restore_sample_default(app_id)


def invalidate_size_cache(app_id: str) -> None:
    cache = SIZE_CACHE_ROOT / f"slot0-{slug(app_id)}"
    if cache.exists():
        shutil.rmtree(cache)
    with CACHE_LOCK:
        SOURCE_MTIME_CACHE.pop(app_id, None)
        UF2_SIZE_CACHE.clear()


def invalidate_app_build_cache(app_id: str) -> None:
    app_slug = slug(app_id)
    for cache in BUILD_ROOT.glob(f"slot*-{app_slug}"):
        if cache.is_dir():
            shutil.rmtree(cache)
    invalidate_size_cache(app_id)


def clean_output_dir(keep: Path | None = None) -> None:
    output_dir = BUILD_ROOT / "output"
    if not output_dir.exists():
        return
    keep_resolved = keep.resolve() if keep else None
    for path in output_dir.iterdir():
        if keep_resolved and path.resolve() == keep_resolved:
            continue
        if path.is_file():
            path.unlink(missing_ok=True)


def slot_build_path(slot_index: int, app: AppDef) -> Path:
    return BUILD_ROOT / f"slot{slot_index}-{slug(app.id)}"


def clean_slot_build_dirs(layout: list[dict]) -> None:
    for item in layout:
        build_path = slot_build_path(item["slot"], item["app"])
        if build_path.exists():
            shutil.rmtree(build_path)


def clamp_sample_names(sample_root: Path) -> None:
    pattern = re.compile(r'^(\s*)"([^"]{20,})"(\s*,\s*//\s*sample name\s*)$')
    for header in sample_root.rglob("*.h"):
        if header.name.startswith("bank_") or header.name in {"sampledefs.h"}:
            original = header.read_text(errors="replace")
            changed = False

            def replace(match: re.Match) -> str:
                nonlocal changed
                changed = True
                return f'{match.group(1)}"{match.group(2)[:19]}"{match.group(3)}'

            updated = "\n".join(pattern.sub(replace, line) for line in original.splitlines())
            if original.endswith("\n"):
                updated += "\n"
            if changed:
                header.write_text(updated)


def rebuild_sample_headers(app_id: str) -> None:
    if app_id == "GridsSampler":
        root = SAMPLE_APPS[app_id]
        tool = root / "wav2header44khz"
        if not tool.exists():
            raise RuntimeError(f"missing converter: {tool.relative_to(ROOT)}")
        subprocess.run([str(tool)], cwd=root, check=True)
        clamp_sample_names(root)
    elif app_id == "OneshotSampler":
        root = SAMPLE_APPS[app_id]
        script = root / "exec"
        if not script.exists():
            raise RuntimeError(f"missing converter script: {script.relative_to(ROOT)}")
        subprocess.run(["/bin/zsh", str(script)], cwd=root, check=True)
        clamp_sample_names(root)
    else:
        raise RuntimeError(f"sample app not supported: {app_id}")
    invalidate_app_build_cache(app_id)


def upload_samples(app_id: str, bank: str, fields: cgi.FieldStorage) -> dict:
    with SAMPLE_LOCK:
        root = SAMPLE_APPS.get(app_id)
        if not root:
            raise RuntimeError(f"sample app not supported: {app_id}")
        target_bank = root if app_id == "GridsSampler" else root / safe_segment(bank)
        target_bank.mkdir(parents=True, exist_ok=True)

        file_fields = fields["files"] if "files" in fields else []
        if not isinstance(file_fields, list):
            file_fields = [file_fields]
        saved = []
        for item in file_fields:
            if not getattr(item, "filename", ""):
                continue
            filename = safe_filename(item.filename)
            target = target_bank / filename
            with target.open("wb") as out:
                shutil.copyfileobj(item.file, out)
            saved.append(filename)
        if not saved:
            raise RuntimeError("no .wav files uploaded")

        rebuild_sample_headers(app_id)
        payload = samples_payload(app_id)
        payload["saved"] = saved
        payload["bank"] = target_bank.name
        return payload


def delete_sample_bank(app_id: str, bank: str) -> dict:
    with SAMPLE_LOCK:
        if app_id != "OneshotSampler":
            raise RuntimeError("bank deletion is only supported for OneshotSampler")
        root = SAMPLE_APPS.get(app_id)
        if not root:
            raise RuntimeError(f"sample app not supported: {app_id}")
        bank_name = safe_segment(bank)
        target = (root / bank_name).resolve()
        try:
            target.relative_to(root.resolve())
        except ValueError as exc:
            raise RuntimeError("invalid bank path") from exc
        if not target.is_dir():
            raise RuntimeError(f"bank not found: {bank_name}")
        shutil.rmtree(target)
        rebuild_sample_headers(app_id)
        payload = samples_payload(app_id)
        payload["deleted"] = bank_name
        return payload


def existing_libraries() -> list[Path]:
    global LIBRARY_PATHS_CACHE
    if LIBRARY_PATHS_CACHE is not None:
        return LIBRARY_PATHS_CACHE
    extra_libraries = [
        Path(item).expanduser()
        for item in os.environ.get("PICO_EXTRA_ARDUINO_LIBRARIES", "").split(os.pathsep)
        if item
    ]
    candidates = [
        ROOT / "Sketches" / "lib",
        *extra_libraries,
        ROOT / ".arduino" / "libraries",
        Path.home() / "Arduino" / "libraries",
        Path.home() / "Documents" / "Arduino" / "libraries",
    ]
    seen = set()
    LIBRARY_PATHS_CACHE = []
    for path in candidates:
        resolved = path.resolve()
        if resolved in seen or not resolved.exists():
            continue
        seen.add(resolved)
        LIBRARY_PATHS_CACHE.append(resolved)
    return LIBRARY_PATHS_CACHE


def prepared_sketch_path(app: AppDef, build_path: Path) -> Path:
    sketch_path = app.sketch_path
    main_ino = sketch_path / f"{sketch_path.name}.ino"
    if main_ino.exists():
        return sketch_path

    ino_files = sorted(sketch_path.glob("*.ino"))
    if len(ino_files) != 1:
        return sketch_path

    temp_root = build_path.parent / "_sketches" / slug(app.id)
    if temp_root.exists():
        shutil.rmtree(temp_root)
    shutil.copytree(
        sketch_path,
        temp_root,
        ignore=shutil.ignore_patterns("build_*", ".DS_Store"),
    )
    copied_ino = temp_root / ino_files[0].name
    copied_ino.rename(temp_root / f"{temp_root.name}.ino")
    return temp_root


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


def build_metadata_path(build_path: Path) -> Path:
    return build_path / ".pico_client_build.json"


def build_metadata(app: AppDef, slot_index: int, slot_offset: int, slot_size: int) -> dict:
    return {
        "version": BUILD_CACHE_VERSION,
        "app": app.id,
        "slot": slot_index,
        "slotOffset": slot_offset,
        "slotSize": slot_size,
        "fqbn": DEFAULT_FQBN,
        "inputMtimeNs": build_inputs_mtime_ns(app),
    }


def write_build_metadata(build_path: Path, metadata: dict, uf2_path: Path) -> None:
    payload = dict(metadata)
    payload["uf2"] = uf2_path.name
    build_metadata_path(build_path).write_text(json.dumps(payload, indent=2) + "\n")


def cached_slot_uf2(
    app: AppDef,
    build_path: Path,
    slot_index: int,
    slot_offset: int,
    slot_size: int,
    metadata: dict,
) -> Path | None:
    uf2_files = sorted(build_path.glob("*.ino.uf2"))
    if not uf2_files:
        return None

    uf2_path = uf2_files[0]
    if uf2_path.stat().st_mtime_ns < metadata["inputMtimeNs"]:
        return None

    metadata_path = build_metadata_path(build_path)
    if metadata_path.exists():
        try:
            existing = json.loads(metadata_path.read_text())
        except json.JSONDecodeError:
            return None
        expected = dict(metadata)
        expected["uf2"] = uf2_path.name
        if existing != expected:
            return None

    try:
        blocks = BOOT.read_uf2(uf2_path)
        span_start, _span_end = BOOT.app_flash_span(blocks)
        if span_start - BOOT.XIP_BASE != slot_offset:
            return None
        if BOOT.app_usage_bytes(blocks) > slot_size:
            return None
    except Exception:
        return None

    return uf2_path


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
    metadata = build_metadata(app, slot_index, slot_offset, slot_size)
    size_cache_path = size_cache_build_path(app)
    if size_cache_path != build_path:
        cached = cached_slot_uf2(app, size_cache_path, slot_index, slot_offset, slot_size, metadata)
        if cached:
            if job:
                job.message = f"Reading {app.name} from sizes"
            return cached

    cached = cached_slot_uf2(app, build_path, slot_index, slot_offset, slot_size, metadata)
    if cached:
        if job:
            job.message = f"Reusing {app.name}"
        return cached

    sketch_path = prepared_sketch_path(app, build_path)

    cmd = [
        sys.executable,
        str(BOOT_TOOL),
        "slot-build",
        str(sketch_path),
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
    write_build_metadata(build_path, metadata, uf2_files[0])
    return uf2_files[0]


def build_slot(slot_index: int, app: AppDef, slot_offset: int, slot_size: int, job: BuildJob | None = None) -> Path:
    build_path = slot_build_path(slot_index, app)
    return build_slot_to_path(slot_index, app, build_path, slot_offset, slot_size, job)


def plan_layout(selected: list[tuple[int, AppDef]], job: BuildJob | None = None) -> list[dict]:
    next_offset = BOOT.PICO_SELECTOR_FIRST_APP_OFFSET
    region_end = BOOT.PICO_SELECTOR_FIRST_APP_OFFSET + APP_REGION_BYTES
    plan = []
    total = max(1, len(selected))
    for app_index, (slot_index, app) in enumerate(selected):
        if job:
            job.update(2, f"Measuring {app.name}")
        size = app_size(app, build_missing=True)
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
        if job:
            job.update(2 + int(((app_index + 1) / total) * 2), f"Measured {app.name}")
    return plan


def generate_firmware(request: dict, job: BuildJob | None = None) -> tuple[Path, str]:
    device = request.get("device")
    slots = request.get("slots")
    active = int(request.get("active", 0))
    if device not in {"pico", "picofx"}:
        raise RuntimeError("invalid device")
    if not isinstance(slots, list) or not 1 <= len(slots) <= MAX_SLOTS:
        raise RuntimeError(f"slots must contain 1 to {MAX_SLOTS} entries")
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

    sample_app_ids = {app.id for _index, app in selected if app.id in SAMPLE_APPS}
    if sample_app_ids:
        SAMPLE_LOCK.acquire()
    try:
        if job:
            job.status = "running"
            job.update(1, "Planning slots")
        layout = plan_layout(selected, job)
        if job:
            job.update(4, "Checking selector")
        selector_path = selector_uf2_path(job, build_if_missing=True)
        if job:
            job.update(5, "Preparing build")
        try:
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
            clean_output_dir(keep=output_path)
            cmd = [
                sys.executable,
                str(BOOT_TOOL),
                "package",
                "--selector",
                str(selector_path),
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
        finally:
            clean_slot_build_dirs(layout)
    finally:
        if sample_app_ids:
            if job:
                job.update(98, "Restoring samples")
            restore_sample_defaults(sample_app_ids)
            SAMPLE_LOCK.release()


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

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path.startswith("/images/"):
            self.send_repo_file(ROOT / parsed.path.lstrip("/"))
            return
        if parsed.path == "/api/manifest":
            self.send_json(HTTPStatus.OK, manifest_payload())
            return
        if parsed.path == "/api/app-size":
            try:
                app_id = parse_qs(parsed.query).get("app", [""])[0]
                app = APP_BY_ID.get(app_id)
                if not app:
                    self.send_json(HTTPStatus.NOT_FOUND, {"error": "app not found"})
                    return
                self.send_json(HTTPStatus.OK, app_payload(app, build_missing=True))
            except Exception as exc:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
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
            output_path = job.output_path
            self.send_download(output_path, job.output_name or output_path.name, delete_after=True)
            job.output_path = None
            return
        if parsed.path == "/api/health":
            self.send_json(HTTPStatus.OK, {"ok": True})
            return
        if parsed.path == "/api/samples":
            try:
                app_id = parse_qs(parsed.query).get("app", [""])[0]
                self.send_json(HTTPStatus.OK, samples_payload(app_id))
            except Exception as exc:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
            return
        super().do_GET()

    def do_HEAD(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path.startswith("/images/"):
            self.send_repo_file(ROOT / parsed.path.lstrip("/"), head_only=True)
            return
        super().do_HEAD()

    def do_OPTIONS(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path.startswith("/api/"):
            self.send_response(HTTPStatus.NO_CONTENT)
            self.send_cors_headers()
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.end_headers()
            return
        self.send_error(HTTPStatus.NOT_FOUND)

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
        if parsed.path == "/api/samples/upload":
            try:
                form = cgi.FieldStorage(
                    fp=self.rfile,
                    headers=self.headers,
                    environ={
                        "REQUEST_METHOD": "POST",
                        "CONTENT_TYPE": self.headers.get("Content-Type", ""),
                        "CONTENT_LENGTH": self.headers.get("Content-Length", "0"),
                    },
                )
                app_id = form.getfirst("app", "")
                bank = form.getfirst("bank", "")
                self.send_json(HTTPStatus.OK, upload_samples(app_id, bank, form))
            except Exception as exc:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
            return
        if parsed.path == "/api/samples/delete-bank":
            try:
                length = int(self.headers.get("Content-Length", "0"))
                payload = json.loads(self.rfile.read(length) or b"{}")
                self.send_json(HTTPStatus.OK, delete_sample_bank(payload.get("app", ""), payload.get("bank", "")))
            except Exception as exc:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
            return
        self.send_error(HTTPStatus.NOT_FOUND)

    def send_json(self, status: HTTPStatus, payload: dict) -> None:
        data = json.dumps(payload, indent=2).encode()
        self.send_response(status)
        self.send_cors_headers()
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_cors_headers(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Expose-Headers", "Content-Disposition")

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

    def send_download(self, path: Path, filename: str, delete_after: bool = False) -> None:
        data = path.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_cors_headers()
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        self.end_headers()
        try:
            self.wfile.write(data)
        finally:
            if delete_after:
                path.unlink(missing_ok=True)

    def guess_type(self, path: str) -> str:
        if path.endswith(".uf2"):
            return "application/octet-stream"
        return mimetypes.guess_type(path)[0] or super().guess_type(path)


def main() -> None:
    with SAMPLE_LOCK:
        ensure_sample_defaults()
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
