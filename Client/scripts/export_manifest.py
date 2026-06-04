#!/usr/bin/env python3
# Copyright 2026 Wenhao Yang
#
# Author: Wenhao Yang
# Contributor: Wenhao Yang
#
# Export the local client API manifest as static JSON for public web deployment.

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from Client import server  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description="Export Pico-Eurorack client manifest JSON")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples-output", type=Path)
    parser.add_argument(
        "--build-missing",
        action="store_true",
        help="Compile missing app size probes before exporting the manifest",
    )
    args = parser.parse_args()

    if args.build_missing:
        server.ensure_sample_defaults()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(server.manifest_payload(build_missing=args.build_missing), indent=2) + "\n")
    if args.samples_output:
        args.samples_output.parent.mkdir(parents=True, exist_ok=True)
        args.samples_output.write_text(json.dumps(server.sample_defaults_payload(), indent=2) + "\n")


if __name__ == "__main__":
    main()
