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
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(server.manifest_payload(), indent=2) + "\n")


if __name__ == "__main__":
    main()
