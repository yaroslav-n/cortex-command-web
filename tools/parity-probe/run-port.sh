#!/bin/sh
# Run the browser port (served on 127.0.0.1:8084, driven by the headless Chrome)
# into <activity> on <scene> with the probe mod and write its terrain map to <out>.
# The browser's own settings are restored and the mod removed afterwards.
set -u
# PROBE_FILE (default ParityProbe.bin) is the file the enabled script writes in Userdata.
# PROBE_SCREENSHOT=1 also fetches the newest /ScreenShots/ParityScreen_*.png to <out>.png.
FILE=${PROBE_FILE:-ParityProbe.bin}
ACT="$1"; SCENE="$2"; OUT="$3"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CCW="$HERE/../ccw.sh"
TMP=$(mktemp -d)
idb_js() { python3 "$HERE/idb.py" "$@"; }
$CCW 60 batch "open http://127.0.0.1:8084/index.html" "wait 4000" > /dev/null
$CCW 60 getfile /Userdata /Userdata/Settings.ini "$TMP/Settings.ini" > /dev/null
python3 "$HERE/settings.py" "$TMP/Settings.ini" "$ACT" "$SCENE" > "$TMP/probe-Settings.ini"
$CCW 30 eval "$(idb_js install "$HERE/ParityProbe.rte" "$TMP/probe-Settings.ini")" > /dev/null
$CCW 30 eval "$(idb_js clearshots)" > /dev/null
$CCW 60 batch "open http://127.0.0.1:8084/index.html" "wait 4000" > /dev/null
$CCW 240 play > /dev/null
i=0
while [ $i -lt 60 ]; do
  sleep 5; i=$((i+1))
  [ "$($CCW 15 eval "$(idb_js exists /Userdata/$FILE)" 2>/dev/null | tail -1)" = "1" ] && break
done
sleep 3
$CCW 180 getfile /Userdata /Userdata/$FILE "$OUT" > /dev/null
if [ -n "${PROBE_SCREENSHOT:-}" ]; then
  KEY=$($CCW 20 eval "$(idb_js shots)" 2>/dev/null | tail -1 | tr -d '"')
  [ -n "$KEY" ] && $CCW 180 getfile /ScreenShots "$KEY" "$OUT.png" > /dev/null
  $CCW 30 eval "$(idb_js clearshots)" > /dev/null
fi
$CCW 60 batch "open http://127.0.0.1:8084/index.html" "wait 4000" > /dev/null
$CCW 30 eval "$(idb_js restore "$TMP/Settings.ini")" > /dev/null
rm -rf "$TMP"
