#!/bin/sh
# Run the unmodified original into <activity> on <scene> with the probe mod and write
# its terrain map to <out>. The reference's settings and Mods folder are restored.
set -u
# ORIGINAL_BINARY runs a different build of the original from the same folder;
# ORIGINAL_LOG keeps its output. ORIGINAL_FPS=<n> paces its frames to n a second
# through tools/native-input's injector, as VSync would (DYLD_ variables must
# be set on the game's own command line: macOS strips them from /bin/sh).
# PROBE_FILE (default ParityProbe.bin) is the file the enabled script writes in Userdata.
# PROBE_SCREENSHOT=1 also moves the newest ScreenShots/ParityScreen_*.png to <out>.png.
FILE=${PROBE_FILE:-ParityProbe.bin}
ACT="$1"; SCENE="$2"; OUT="$3"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# The untouched original game: a separate checkout, ORIGINAL_CODE or next to this
# repository (see notes/original-game.md).
REF=$(CDPATH= cd -- "${ORIGINAL_CODE:-$HERE/../../../original-code}" && pwd)
BACKUP=$(mktemp)
cp "$REF/Userdata/Settings.ini" "$BACKUP"
restore() { cp "$BACKUP" "$REF/Userdata/Settings.ini"; rm -rf "$REF/Mods/ParityProbe.rte" "$BACKUP"; }
trap restore EXIT
# The resolution the comparisons use (the port's 1296x837 viewport at 1.5x); a
# fresh checkout's own default is 960x540. PROBE_SETTINGS given later override it.
PROBE_SETTINGS="ResolutionX=864 ResolutionY=558 ${PROBE_SETTINGS:-}" \
  python3 "$HERE/settings.py" "$BACKUP" "$ACT" "$SCENE" --no-vsync > "$REF/Userdata/Settings.ini"
mkdir -p "$REF/Mods"; rm -rf "$REF/Mods/ParityProbe.rte"; cp -R "$HERE/ParityProbe.rte" "$REF/Mods/"
rm -f "$REF/Userdata/$FILE" "$REF"/ScreenShots/ParityScreen_*.png
if [ -n "${ORIGINAL_FPS:-}" ]; then
  INJECT="$HERE/../native-input/libinject.dylib"
  [ -f "$INJECT" ] || "$HERE/../native-input/build.sh" > /dev/null
  (cd "$REF" && DYLD_INSERT_LIBRARIES="$INJECT" CORTEX_INPUT_FPS="$ORIGINAL_FPS" \
    exec "${ORIGINAL_BINARY:-./Cortex Command.app/Contents/MacOS/CortexCommand}" > "${ORIGINAL_LOG:-/dev/null}" 2>&1) &
else
  (cd "$REF" && exec "${ORIGINAL_BINARY:-./Cortex Command.app/Contents/MacOS/CortexCommand}" > "${ORIGINAL_LOG:-/dev/null}" 2>&1) &
fi
PID=$!
i=0; while kill -0 $PID 2>/dev/null && [ $i -lt 120 ]; do sleep 5; i=$((i+1)); done
# The game ignores SIGTERM while a modal dialog is up (an assertion box, say), so
# follow up with SIGKILL rather than leave it running.
if kill -0 $PID 2>/dev/null; then kill $PID; sleep 3; kill -9 $PID 2>/dev/null; fi
[ -f "$REF/Userdata/$FILE" ] && mv "$REF/Userdata/$FILE" "$OUT"
if [ -n "${PROBE_SCREENSHOT:-}" ]; then
  SHOT=$(ls -t "$REF"/ScreenShots/ParityScreen_*.png 2>/dev/null | head -1)
  [ -n "$SHOT" ] && mv "$SHOT" "$OUT.png"
  rm -f "$REF"/ScreenShots/ParityScreen_*.png
fi
