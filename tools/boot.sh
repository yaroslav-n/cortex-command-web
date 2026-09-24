#!/bin/sh
# Load the game in the driven Chrome and stop at the main menu.
# Usage: boot.sh [url-query]   e.g. boot.sh '?perf-debug'
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC="$ROOT/tools/cc.sh"
QUERY=${1:-}
"$CC" batch \
  "open http://127.0.0.1:8084/index.html$QUERY" \
  "wait 2000" \
  "play" \
  "wait 10000" "wait 5000"
# The game's own "Skip intro" setting (Settings > Misc.) is enabled in the
# driven browser's stored settings, so no keypress is needed to reach the menu.
# Never send Escape here: on the main menu the engine treats it as Quit, exactly
# as it does natively.
