#!/bin/sh
# Run the untouched original with scripted input (see inject.c) and collect the
# ScreenDumps its F12 key writes. The reference's settings are restored afterwards.
# Usage: run-original.sh <script> <output directory> [Key=Value ...]
# EXTRA_USERDATA=<directory>: its files are copied into the reference's Userdata for
# the run (a saved campaign with the Index.ini that registers it, say); files it adds
# are removed and files it replaces are put back afterwards.
# Settings: SkipIntro=1, 864x558 at 1.5x, VSync off (a locked screen never sends the refresh the swap
# waits for), plus any Key=Value given. The injector paces frames to 60 a second
# instead, as VSync on a 60 Hz display would (CORTEX_INPUT_FPS, 0 for uncapped).
set -u
SCRIPT=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)/$(basename -- "$1"); OUT=$2; shift 2
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# The untouched original game: a separate checkout, ORIGINAL_CODE or next to this
# repository (see notes/original-game.md).
REF=$(CDPATH= cd -- "${ORIGINAL_CODE:-$HERE/../../../original-code}" && pwd)
[ -f "$HERE/libinject.dylib" ] || "$HERE/build.sh" > /dev/null
mkdir -p "$OUT"
BACKUP=$(mktemp)
cp "$REF/Userdata/Settings.ini" "$BACKUP"
ADDED=$(mktemp); REPLACED=$(mktemp -d)
restore() {
  cp "$BACKUP" "$REF/Userdata/Settings.ini"; rm -f "$BACKUP" "$REF"/ScreenShots/ScreenDump_*.png
  while IFS= read -r added; do rm -f "$REF/Userdata/$added"; done < "$ADDED"; rm -f "$ADDED"
  (cd "$REPLACED" && find . -type f | sed 's#^\./##') | while IFS= read -r f; do cp "$REPLACED/$f" "$REF/Userdata/$f"; done
  rm -rf "$REPLACED"
}
trap restore EXIT
if [ -n "${EXTRA_USERDATA:-}" ]; then
  (cd "$EXTRA_USERDATA" && find . -type f | sed 's#^\./##') | while IFS= read -r f; do
    if [ -e "$REF/Userdata/$f" ]; then
      mkdir -p "$(dirname "$REPLACED/$f")"; cp "$REF/Userdata/$f" "$REPLACED/$f"
    else
      echo "$f" >> "$ADDED"
    fi
    mkdir -p "$(dirname "$REF/Userdata/$f")"; cp "$EXTRA_USERDATA/$f" "$REF/Userdata/$f"
  done
fi
# 864x558 at 1.5x is what the browser side gets from its 1296x837 viewport.
python3 "$HERE/settings.py" "$BACKUP" SkipIntro=1 EnableVSync=0 ResolutionX=864 ResolutionY=558 ResolutionMultiplier=1.500000 "$@" > "$REF/Userdata/Settings.ini"
rm -f "$REF"/ScreenShots/ScreenDump_*.png
BEFORE=$(mktemp); ls "$REF"/ScreenShots/ > "$BEFORE" 2>/dev/null
TOTAL=$(awk '$1=="wait"{s+=$2} END {print int(s/1000)+120}' "$SCRIPT")
(cd "$REF" && DYLD_INSERT_LIBRARIES="$HERE/libinject.dylib" CORTEX_INPUT_SCRIPT="$SCRIPT" CORTEX_INPUT_FPS="${CORTEX_INPUT_FPS:-60}" \
  exec "./Cortex Command.app/Contents/MacOS/CortexCommand" > "$OUT/original.log" 2>&1) &
PID=$!
i=0; while kill -0 $PID 2>/dev/null && [ $i -lt "$TOTAL" ]; do sleep 1; i=$((i+1)); done
if kill -0 $PID 2>/dev/null; then kill $PID; sleep 3; kill -9 $PID 2>/dev/null; fi
n=0
for shot in $(ls "$REF"/ScreenShots/ScreenDump_*.png 2>/dev/null | sort); do
  n=$((n+1)); mv "$shot" "$OUT/original_$(printf %02d $n).png"
done
# World and scene preview dumps (Alt+W, RAlt+W) made during the run.
for dump in $(ls "$REF"/ScreenShots/ 2>/dev/null | grep -v -x -F -f "$BEFORE" | grep '^\(WorldDump\|ScenePreviewDump\)_'); do
  mv "$REF/ScreenShots/$dump" "$OUT/original_$(echo "$dump" | cut -d_ -f1).png"
done
rm -f "$BEFORE"
echo "original: $n screenshots, dumps: $(ls "$OUT" | grep -c 'original_[A-Z]')"
