#!/bin/sh
# Install the build toolchain into toolchains/ (ignored by git):
#   toolchains/emsdk   Emscripten SDK, pinned to the version the port is built and tested with
#   toolchains/python  a Python virtual environment with CMake and Ninja
# Safe to run again; it only installs what is missing.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EMSDK_VERSION=4.0.15
CMAKE_VERSION=3.31.6
TOOLCHAINS="$ROOT/toolchains"
mkdir -p "$TOOLCHAINS"

if [ ! -x "$TOOLCHAINS/emsdk/emsdk" ]; then
  git clone https://github.com/emscripten-core/emsdk.git "$TOOLCHAINS/emsdk"
fi
if [ ! -f "$TOOLCHAINS/emsdk/upstream/emscripten/emscripten-version.txt" ] ||
   ! grep -q "\"$EMSDK_VERSION\"" "$TOOLCHAINS/emsdk/upstream/emscripten/emscripten-version.txt"; then
  "$TOOLCHAINS/emsdk/emsdk" install "$EMSDK_VERSION"
  "$TOOLCHAINS/emsdk/emsdk" activate "$EMSDK_VERSION"
fi

if [ ! -x "$TOOLCHAINS/python/bin/cmake" ]; then
  python3 -m venv "$TOOLCHAINS/python"
  "$TOOLCHAINS/python/bin/pip" install --quiet "cmake==$CMAKE_VERSION" ninja
fi

echo "Toolchains ready: Emscripten $EMSDK_VERSION, $("$TOOLCHAINS/python/bin/cmake" --version | head -1)"
