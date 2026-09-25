#!/bin/sh
# Regenerate engine/PORT-CHANGES.patch: every difference between the port's engine
# and the untouched original game. Run after changing anything under engine/.
# The original is a separate checkout of the Cortex Command Community Project (see
# notes/original-game.md), found at ORIGINAL_CODE or next to this repository.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ORIGINAL=$(CDPATH= cd -- "${ORIGINAL_CODE:-$ROOT/../original-code}" && pwd)
OUT="$ROOT/engine/PORT-CHANGES.patch"
: > "$OUT"
cd "$(dirname "$ORIGINAL")"
REF=$(basename "$ORIGINAL")
# Of the game's data, the port changes only shaders, GUI layouts and the Generic Actor
# Spawner, which it keeps out of the editors' object list.
for part in Source Resources external meson.build meson_options.txt Data/Base.rte/Shaders Data/Base.rte/GUIs \
    Data/Base.rte/Scenes/Objects/Bunkers/BunkerSystems/ActorSpawner; do
  # diff exits 1 when files differ; that is the expected case here.
  # Framework symlink loops in vendored Xcode projects make diff complain; noise.
  # -P shows files the port added in full, and reports files the port removed as
  # a single "Only in original/..." line rather than their whole contents. Paths are
  # written as original/... and port/... so the patch does not depend on where the
  # two checkouts live.
  diff -ruP --exclude='*.o' "$REF/$part" "$ROOT/engine/$part" 2>/dev/null \
    | sed -e "s#^\(diff -ruP --exclude=\*\.o \)$REF/#\1original/#" -e "s# $ROOT/engine/# port/#" \
          -e "s#^--- $REF/#--- original/#" -e "s#^+++ $ROOT/engine/#+++ port/#" \
          -e "s#^Only in $REF/#Only in original/#" -e "s#^Only in $ROOT/engine/#Only in port/#" >> "$OUT" || true
done
echo "files changed or added: $(grep -c '^diff -ruP' "$OUT")"
echo "files or directories removed: $(grep -c '^Only in original/' "$OUT")"
echo "lines: $(wc -l < "$OUT" | tr -d ' ')"
