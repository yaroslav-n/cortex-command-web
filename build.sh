#!/bin/sh
# Build the browser port.
#   ./build.sh [--debug] [--worker] [cmake --build arguments, e.g. --target cortex]
# The default is the release build, into build/wasm and dist/. --debug adds
# Emscripten's runtime assertions and builds into build/wasm-debug and dist-debug/.
# --worker runs the engine on a worker thread instead of the page's main thread
# (CORTEX_WORKER; see notes/threads.md), built into build/wasm-worker and dist-worker/.
# The toolchain lives in toolchains/ (tools/setup-toolchains.sh installs it); set
# EMSDK to use an Emscripten SDK elsewhere.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$ROOT/build/wasm"
DIST_DIR="$ROOT/dist"
DIAGNOSTICS=OFF
WORKER=OFF
while [ $# -gt 0 ]; do
  case "$1" in
    --debug) DIAGNOSTICS=ON; BUILD_DIR="$BUILD_DIR-debug"; DIST_DIR="$DIST_DIR-debug"; shift ;;
    --worker) WORKER=ON; BUILD_DIR="$BUILD_DIR-worker"; DIST_DIR="$DIST_DIR-worker"; shift ;;
    *) break ;;
  esac
done
CONFIGURE="-DCORTEX_WEB_DIAGNOSTICS=$DIAGNOSTICS -DCORTEX_WORKER=$WORKER -DCORTEX_DIST_DIR=$DIST_DIR"
EMSDK=${EMSDK:-"$ROOT/toolchains/emsdk"}
if [ ! -f "$EMSDK/emsdk_env.sh" ]; then
  echo "No Emscripten SDK at $EMSDK; run tools/setup-toolchains.sh first." >&2
  exit 1
fi
# The Xcode licence need not be accepted: the Command Line Tools are enough.
if [ "$(uname)" = Darwin ] && [ -d /Library/Developer/CommandLineTools ]; then
  export DEVELOPER_DIR=${DEVELOPER_DIR:-/Library/Developer/CommandLineTools}
fi
[ -d "$ROOT/toolchains/python/bin" ] && export PATH="$ROOT/toolchains/python/bin:$PATH"
. "$EMSDK/emsdk_env.sh" > /dev/null 2>&1
# shellcheck disable=SC2086
emcmake cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release $CONFIGURE
cmake --build "$BUILD_DIR" -j 8 "$@"
