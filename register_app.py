#!/usr/bin/env python3
# Copyright 2026 Wenhao Yang
#
# Author: Wenhao Yang
# Contributor: Wenhao Yang
#
# Register a new Pico-Eurorack app in the backend and frontend catalogs,
# then optionally run the existing capacity measurement pipeline.
# How to use:
# 
'''
python3 register_app.py
  --device pico
  --name NewVoice
  --description "New oscillator voice"
  --in "V/Oct in"
  --page-button "Click to change pages"
  --aux "FM in"
  --out "Audio out"
  --page "MAIN|Pitch|Shape|Timbre|Level"
  --allow-missing-sketch
'''



from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SERVER_PATH = ROOT / "Client" / "server.py"
APP_CONFIG_PATH = ROOT / "Client" / "app-config.js"


@dataclass
class PageSpec:
    name: str
    led: str
    params: list[str]


def js_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def py_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def slug_app_id(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_]+", "", value)
    if not cleaned:
        raise ValueError("app id cannot be empty")
    if cleaned[0].isdigit():
        cleaned = f"App{cleaned}"
    return cleaned


def parse_page(value: str) -> PageSpec:
    parts = [part.strip() for part in value.split("|")]
    if len(parts) == 5:
        name, p1, p2, p3, p4 = parts
        led = "red"
    elif len(parts) == 6:
        name, led, p1, p2, p3, p4 = parts
    else:
        raise argparse.ArgumentTypeError(
            "--page must be 'Page|Param1|Param2|Param3|Param4' "
            "or 'Page|Led|Param1|Param2|Param3|Param4'"
        )
    if not name:
        raise argparse.ArgumentTypeError("page name cannot be empty")
    led = led or "red"
    params = [p1 or "Unused", p2 or "Unused", p3 or "Unused", p4 or "Unused"]
    return PageSpec(name=name, led=led, params=params)


def find_matching_bracket(text: str, open_index: int) -> int:
    opening = text[open_index]
    closing = {"[": "]", "{": "}", "(": ")"}[opening]
    depth = 0
    quote = ""
    escape = False
    line_comment = False
    block_comment = False

    i = open_index
    while i < len(text):
        char = text[i]
        next_char = text[i + 1] if i + 1 < len(text) else ""

        if line_comment:
            if char == "\n":
                line_comment = False
            i += 1
            continue
        if block_comment:
            if char == "*" and next_char == "/":
                block_comment = False
                i += 2
            else:
                i += 1
            continue
        if quote:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = ""
            i += 1
            continue
        if char in {"'", '"', "`"}:
            quote = char
            i += 1
            continue
        if char == "/" and next_char == "/":
            line_comment = True
            i += 2
            continue
        if char == "/" and next_char == "*":
            block_comment = True
            i += 2
            continue
        if char == opening:
            depth += 1
        elif char == closing:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError(f"could not find matching {closing}")


def find_apps_array(text: str) -> tuple[int, int]:
    match = re.search(r"APPS\s*=\s*\[", text)
    if not match:
        raise ValueError("could not find APPS list in Client/server.py")
    open_index = text.find("[", match.start())
    close_index = find_matching_bracket(text, open_index)
    return open_index, close_index


def find_frontend_device_apps(text: str, device: str) -> tuple[int, int]:
    device_match = re.search(rf"\b{re.escape(device)}\s*:\s*\{{", text)
    if not device_match:
        raise ValueError(f"could not find frontend device block: {device}")
    apps_match = re.search(r"\bapps\s*:\s*\[", text[device_match.end() :])
    if not apps_match:
        raise ValueError(f"could not find frontend apps list for: {device}")
    open_index = text.find("[", device_match.end() + apps_match.start())
    close_index = find_matching_bracket(text, open_index)
    return open_index, close_index


def insert_before_list_close(text: str, open_index: int, close_index: int, entry: str, close_indent: str) -> str:
    body = text[open_index + 1 : close_index]
    before = text[:close_index].rstrip()
    needs_comma = bool(body.strip()) and not before.endswith(",")
    comma = "," if needs_comma else ""
    return f"{before}{comma}\n{entry}\n{close_indent}{text[close_index:]}"


def build_backend_entry(app_id: str, device: str, name: str, sketch: str, build: str | None) -> str:
    build_value = "None" if not build else py_string(build)
    return (
        f"    AppDef({py_string(app_id)}, {py_string(device)}, {py_string(name)}, "
        f"{py_string(sketch)}, {build_value}),"
    )


def build_frontend_entry(name: str, description: str, pages: list[PageSpec], in_func: str, page_func: str, aux: str, out: str, info_text: str) -> str:
    page_lines = []
    for page in pages:
        params = ", ".join(js_string(item) for item in page.params)
        page_lines.append(
            f"            page({js_string(page.name)}, [{params}], led({js_string(page.led)}))"
        )
    pages_text = ",\n".join(page_lines)
    info_expr = f"info(true, {js_string(info_text)})" if info_text.strip() else 'info(false, "")'
    return (
        f"          app({js_string(name)}, {js_string(description)}, [\n"
        f"{pages_text}\n"
        f"          ], io({js_string(in_func)}, {js_string(page_func)}, {js_string(aux)}, {js_string(out)}), {info_expr})"
    )


def render_backend_update(app_id: str, device: str, name: str, sketch: str, build: str | None) -> tuple[str, str]:
    text = SERVER_PATH.read_text()
    if re.search(rf"AppDef\(\s*{re.escape(py_string(app_id))}\s*,", text):
        raise ValueError(f"backend already contains app id: {app_id}")
    open_index, close_index = find_apps_array(text)
    entry = build_backend_entry(app_id, device, name, sketch, build)
    updated = insert_before_list_close(text, open_index, close_index, entry, "")
    return updated, entry


def render_frontend_update(device: str, name: str, description: str, pages: list[PageSpec], in_func: str, page_func: str, aux: str, out: str, info_text: str) -> tuple[str, str]:
    text = APP_CONFIG_PATH.read_text()
    if re.search(rf"app\(\s*{re.escape(js_string(name))}\s*,", text):
        raise ValueError(f"frontend already contains app name: {name}")
    open_index, close_index = find_frontend_device_apps(text, device)
    entry = build_frontend_entry(name, description, pages, in_func, page_func, aux, out, info_text)
    updated = insert_before_list_close(text, open_index, close_index, entry, "        ")
    return updated, entry


def measure_capacity(app_id: str) -> None:
    if str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))
    from Client import server  # noqa: WPS433

    app = server.APP_BY_ID.get(app_id)
    if not app:
        raise RuntimeError(f"backend app id was not loaded: {app_id}")
    server.ensure_sample_defaults()
    payload = server.app_payload(app, build_missing=True)
    size = payload.get("sizeBytes")
    allocated = payload.get("allocatedBytes")
    if isinstance(size, int):
        print(f"Capacity: {size} bytes, allocated {allocated} bytes")
    else:
        print("Capacity: unknown; backend could not measure this app")
    print(json.dumps(payload, ensure_ascii=False, indent=2))


def default_sketch_path(device: str, app_id: str) -> str:
    family = "Pico" if device == "pico" else "PicoFX"
    return f"Sketches/{family}/{app_id}"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Register a new Pico-Eurorack app in backend/frontend catalogs and optionally measure capacity.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Example:\n"
            "  python3 register_app.py --device pico --name NewVoice --description \"New voice\" \\\n"
            "    --in \"V/Oct in\" --page-button \"Click to change pages\" --aux \"FM in\" --out \"Audio out\" \\\n"
            "    --page \"RED|red|Pitch|Shape|Timbre|Level\" --info \"Hold PAGE to save\" --measure\n"
        ),
    )
    parser.add_argument("--device", choices=("pico", "picofx"), required=True)
    parser.add_argument("--name", required=True, help="Frontend app name")
    parser.add_argument("--app-id", help="Backend app id; defaults to a sanitized --name")
    parser.add_argument("--description", required=True, help="Short app description shown on the frontend card")
    parser.add_argument("--sketch", help="Sketch path; defaults to Sketches/Pico|PicoFX/<app-id>")
    parser.add_argument("--build", help="Optional prebuilt UF2 path for backend AppDef")
    parser.add_argument("--in", dest="in_func", required=True, help="IN jack function")
    parser.add_argument("--page-button", required=True, help="PAGE button function")
    parser.add_argument("--aux", required=True, help="AUX/CV jack function")
    parser.add_argument("--out", required=True, help="OUT jack function")
    parser.add_argument("--info", default="", help='Optional info popover text shown behind the "i" button')
    parser.add_argument(
        "--page",
        action="append",
        type=parse_page,
        required=True,
        help="Page definition: Page|Param1|Param2|Param3|Param4 or Page|Led|Param1|Param2|Param3|Param4",
    )
    parser.add_argument("--page-count", type=int, help="Optional page count validation")
    parser.add_argument("--measure", action="store_true", help="Run capacity measurement after writing files")
    parser.add_argument("--dry-run", action="store_true", help="Print changes without writing files")
    parser.add_argument("--allow-missing-sketch", action="store_true", help="Allow registration before the sketch directory exists")
    args = parser.parse_args()

    app_id = args.app_id or slug_app_id(args.name)
    sketch = args.sketch or default_sketch_path(args.device, app_id)
    sketch_path = ROOT / sketch
    if not args.allow_missing_sketch and not sketch_path.exists():
        raise SystemExit(f"Sketch path does not exist: {sketch}. Use --allow-missing-sketch to register anyway.")
    if args.page_count is not None and args.page_count != len(args.page):
        raise SystemExit(f"--page-count is {args.page_count}, but {len(args.page)} --page entries were provided.")

    backend_text, backend_entry = render_backend_update(app_id, args.device, args.name, sketch, args.build)
    frontend_text, frontend_entry = render_frontend_update(
        args.device,
        args.name,
        args.description,
        args.page,
        args.in_func,
        args.page_button,
        args.aux,
        args.out,
        args.info,
    )

    if args.dry_run:
        print("[dry-run] would update Client/server.py")
        print(backend_entry)
        print("[dry-run] would update Client/app-config.js")
        print(frontend_entry)
    else:
        SERVER_PATH.write_text(backend_text)
        APP_CONFIG_PATH.write_text(frontend_text)

    print(f"Registered {args.name} ({app_id}) for {args.device}.")
    if args.measure and not args.dry_run:
        measure_capacity(app_id)
    elif args.measure:
        print("[dry-run] skipped capacity measurement")


if __name__ == "__main__":
    main()
