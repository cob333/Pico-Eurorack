#!/usr/bin/env python3
# Copyright 2026 Wenhao Yang
#
# Author: Wenhao Yang
# Contributor: Wenhao Yang
#
# GitHub Actions entrypoint for generating Pico-Eurorack bundled UF2 files.

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from Client import server  # noqa: E402


def parse_slots(value: str) -> list[str]:
    text = value.strip()
    if not text:
        raise argparse.ArgumentTypeError("slots cannot be empty")

    if text.startswith("["):
        try:
            parsed = json.loads(text)
        except json.JSONDecodeError as exc:
            raise argparse.ArgumentTypeError(f"invalid slots JSON: {exc}") from exc
        if not isinstance(parsed, list):
            raise argparse.ArgumentTypeError("slots JSON must be an array")
        slots = [str(item).strip() if item is not None else "" for item in parsed]
    else:
        slots = [item.strip() for item in text.split(",")]

    if not 1 <= len(slots) <= server.MAX_SLOTS:
        raise argparse.ArgumentTypeError(f"slots must contain 1 to {server.MAX_SLOTS} entries")
    return slots


def slugify(value: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9_.-]+", "-", value).strip("-")
    return slug or "firmware"


def validate_request(device: str, slots: list[str], active: int) -> None:
    if device not in {"pico", "picofx"}:
        raise ValueError("device must be pico or picofx")
    if not 0 <= active < len(slots):
        raise ValueError("active slot is out of range")
    for app_id in slots:
        if not app_id:
            continue
        app = server.APP_BY_ID.get(app_id)
        if app is None:
            known = ", ".join(sorted(server.APP_BY_ID))
            raise ValueError(f"unknown app id: {app_id}. Known apps: {known}")
        if app.device != device:
            raise ValueError(f"{app_id} belongs to {app.device}, not {device}")


def verify_action_libraries() -> None:
    libraries = server.existing_libraries()
    print("Arduino library search paths:")
    for library in libraries:
        print(f"  - {library}")

    neopixel_headers = [library / "Adafruit_NeoPixel" / "Adafruit_NeoPixel.h" for library in libraries]
    if not any(path.exists() for path in neopixel_headers):
        configured = os.environ.get("PICO_EXTRA_ARDUINO_LIBRARIES", "")
        expected = "\n".join(f"  - {path}" for path in neopixel_headers)
        raise RuntimeError(
            "Adafruit_NeoPixel.h was not found in the Arduino library search paths.\n"
            f"PICO_EXTRA_ARDUINO_LIBRARIES={configured!r}\n"
            f"Checked:\n{expected}"
        )


def aws_env() -> dict[str, str]:
    env = os.environ.copy()
    env["AWS_ACCESS_KEY_ID"] = os.environ.get("R2_ACCESS_KEY_ID", "")
    env["AWS_SECRET_ACCESS_KEY"] = os.environ.get("R2_SECRET_ACCESS_KEY", "")
    return env


def r2_endpoint() -> str:
    account_id = os.environ.get("R2_ACCOUNT_ID", "")
    if not account_id:
        raise RuntimeError("R2_ACCOUNT_ID is required for sample uploads")
    return f"https://{account_id}.r2.cloudflarestorage.com"


def r2_bucket() -> str:
    bucket = os.environ.get("R2_BUCKET", "")
    if not bucket:
        raise RuntimeError("R2_BUCKET is required for sample uploads")
    return bucket


def read_r2_json(key: str) -> dict:
    result = subprocess.run(
        [
            "aws",
            "--endpoint-url",
            r2_endpoint(),
            "s3",
            "cp",
            f"s3://{r2_bucket()}/{key}",
            "-",
        ],
        check=True,
        env=aws_env(),
        text=True,
        stdout=subprocess.PIPE,
    )
    return json.loads(result.stdout)


def download_r2_file(key: str, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "aws",
            "--endpoint-url",
            r2_endpoint(),
            "s3",
            "cp",
            f"s3://{r2_bucket()}/{key}",
            str(output),
        ],
        check=True,
        env=aws_env(),
    )


def apply_sample_deletes(deletes: list[dict]) -> set[str]:
    touched: set[str] = set()
    for item in deletes:
        if not isinstance(item, dict):
            raise RuntimeError("invalid sample delete entry")
        app_id = str(item.get("app") or "")
        if app_id not in server.SAMPLE_APPS:
            raise RuntimeError(f"sample app not supported: {app_id}")
        root = server.SAMPLE_APPS[app_id]
        delete_type = str(item.get("type") or "")
        if delete_type == "file":
            filename = server.safe_filename(str(item.get("name") or ""))
            bank = server.safe_segment(str(item.get("bank") or "")) if app_id == "OneshotSampler" and item.get("bank") else ""
            target = (root / bank / filename).resolve() if bank else (root / filename).resolve()
        elif delete_type == "bank" and app_id == "OneshotSampler":
            bank = server.safe_segment(str(item.get("bank") or ""))
            target = (root / bank).resolve()
        else:
            raise RuntimeError("invalid sample delete type")
        try:
            target.relative_to(root.resolve())
        except ValueError as exc:
            raise RuntimeError("invalid sample delete path") from exc
        if target.is_dir():
            shutil.rmtree(target)
        else:
            target.unlink(missing_ok=True)
        touched.add(app_id)
    return touched


def apply_sample_bundle(sample_key: str) -> None:
    if not sample_key:
        return
    if not sample_key.startswith("samples/") or not sample_key.endswith("/manifest.json"):
        raise RuntimeError("invalid sample bundle key")

    manifest = read_r2_json(sample_key)
    uploads = manifest.get("uploads")
    if not isinstance(uploads, list):
        raise RuntimeError("sample bundle manifest is missing uploads")
    deletes = manifest.get("deletes") or []
    if not isinstance(deletes, list):
        raise RuntimeError("sample bundle manifest has invalid deletes")

    touched: set[str] = apply_sample_deletes(deletes)
    for upload in uploads:
        if not isinstance(upload, dict):
            raise RuntimeError("invalid sample upload entry")
        app_id = str(upload.get("app") or "")
        files = upload.get("files")
        if app_id not in server.SAMPLE_APPS:
            raise RuntimeError(f"sample app not supported: {app_id}")
        if not isinstance(files, list) or not files:
            continue

        root = server.SAMPLE_APPS[app_id]
        bank = server.safe_segment(str(upload.get("bank") or "Custom"))
        target_root = root if app_id == "GridsSampler" else root / bank
        target_root.mkdir(parents=True, exist_ok=True)
        for item in files:
            if not isinstance(item, dict):
                raise RuntimeError("invalid sample file entry")
            key = str(item.get("key") or "")
            filename = server.safe_filename(str(item.get("name") or Path(key).name))
            if not key.startswith("samples/uploads/"):
                raise RuntimeError("invalid sample file key")
            download_r2_file(key, target_root / filename)
        touched.add(app_id)

    for app_id in sorted(touched):
        server.rebuild_sample_headers(app_id)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a Pico-Eurorack UF2 bundle in GitHub Actions")
    parser.add_argument("--request-id", required=True)
    parser.add_argument("--device", choices=("pico", "picofx"), required=True)
    parser.add_argument(
        "--slots",
        type=parse_slots,
        required=True,
        help='Comma-separated app ids or JSON array, for example "TripleOSC,Plaits,Rings"',
    )
    parser.add_argument("--active", type=int, default=0, help="Zero-based active slot index")
    parser.add_argument("--sample-key", default="", help="Optional R2 sample bundle manifest key")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist" / "firmware")
    args = parser.parse_args()

    validate_request(args.device, args.slots, args.active)

    verify_action_libraries()
    server.ensure_sample_defaults()
    apply_sample_bundle(args.sample_key)
    output_path, output_name = server.generate_firmware(
        {
            "device": args.device,
            "slots": args.slots,
            "active": args.active,
        }
    )

    selected = [slot for slot in args.slots if slot]
    artifact_name = slugify(f"Pico-Eurorack-{args.device}-{'-'.join(selected)}-{args.request_id}.uf2")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    artifact_path = args.output_dir / artifact_name
    shutil.copy2(output_path, artifact_path)

    summary = {
        "requestId": args.request_id,
        "device": args.device,
        "slots": args.slots,
        "active": args.active,
        "generated": output_name,
        "artifact": artifact_path.name,
        "sampleKey": args.sample_key,
    }
    (args.output_dir / "build-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
