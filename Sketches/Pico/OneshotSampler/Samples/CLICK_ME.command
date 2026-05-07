#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

/bin/zsh "$SCRIPT_DIR/exec"

echo
read -r "?Press Enter to exit... "
