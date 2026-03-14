#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Render an RP2350 linker script for an app slot")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--flash-length", required=True)
    parser.add_argument("--eeprom-start", required=True)
    parser.add_argument("--fs-start", required=True)
    parser.add_argument("--fs-end", required=True)
    parser.add_argument("--ram-length", required=True)
    parser.add_argument("--psram-length", required=True)
    parser.add_argument("--app-base", required=True)
    args = parser.parse_args()

    text = args.input.read_text(encoding="utf-8")
    text = text.replace("FLASH(rx) : ORIGIN = 0x10000000, LENGTH = __FLASH_LENGTH__", f"FLASH(rx) : ORIGIN = {args.app_base}, LENGTH = __FLASH_LENGTH__")
    text = text.replace("__FLASH_LENGTH__", args.flash_length)
    text = text.replace("__EEPROM_START__", args.eeprom_start)
    text = text.replace("__FS_START__", args.fs_start)
    text = text.replace("__FS_END__", args.fs_end)
    text = text.replace("__RAM_LENGTH__", args.ram_length)
    text = text.replace("__PSRAM_LENGTH__", args.psram_length)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
