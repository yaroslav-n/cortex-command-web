#!/bin/sh
# Build the input injector against the SDL the original links (Homebrew's).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
SDL=/opt/homebrew/opt/sdl3
clang -dynamiclib -O2 -Wall -o "$HERE/libinject.dylib" "$HERE/inject.c" -I"$SDL/include" -L"$SDL/lib" -lSDL3
echo "$HERE/libinject.dylib"
