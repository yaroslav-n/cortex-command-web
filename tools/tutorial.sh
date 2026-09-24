#!/bin/sh
# From the main menu, launch the Tutorial mission with Player 1 on Team 1.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC="$ROOT/tools/cc.sh"
"$CC" batch \
  "click 640 205" "wait 3000" \
  "click 1007 237" "wait 4000" \
  "click 501 333" "wait 1500" \
  "click 640 448" "wait 10000" "wait 10000" "wait 5000"
