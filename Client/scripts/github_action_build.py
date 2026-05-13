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
import re
import shutil
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
    parser.add_argument("--sample-key", default="", help="Reserved R2 sample upload key for future sampler support")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist" / "firmware")
    args = parser.parse_args()

    validate_request(args.device, args.slots, args.active)
    if args.sample_key:
        raise RuntimeError("cloud sample uploads are not enabled in this first deployment")

    server.ensure_sample_defaults()
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
