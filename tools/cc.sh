#!/bin/sh
# Thin wrapper around browser_driver.mjs so game sessions are short to type.
# Screenshots land in the session scratch directory unless a path is given.
# Uses the Node that ships with the Emscripten SDK in toolchains/, or NODE if set.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -z "${NODE:-}" ]; then
  NODE=$(ls -d "$ROOT"/toolchains/emsdk/node/*/bin/node 2>/dev/null | tail -1)
fi
exec "${NODE:-node}" "$ROOT/tools/browser_driver.mjs" "$@"
