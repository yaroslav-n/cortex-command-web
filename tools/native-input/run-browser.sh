#!/bin/sh
# Run the browser port with the same input script as run-original.sh, through the
# headless Chrome driver, and collect the ScreenDumps its F12 key writes. The
# browser's settings are restored afterwards. Use the Metal instance for pixels.
# Usage: run-browser.sh <script> <output directory> [Key=Value ...]
set -u
SCRIPT=$1; OUT=$2; shift 2
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CC="$HERE/../cc.sh"; IDB="$HERE/../parity-probe/idb.py"
URL=${PORT_URL:-http://127.0.0.1:8084/index.html}
mkdir -p "$OUT"; TMP=$(mktemp -d)
# 837 for the game (864x558 at 1.5) plus the page's 50 px strip under it.
$CC resize 1296 887 > /dev/null
$CC batch "open $URL" "wait 3000" > /dev/null
$CC getfile /Userdata /Userdata/Settings.ini "$TMP/Settings.ini" > /dev/null
python3 "$HERE/settings.py" "$TMP/Settings.ini" SkipIntro=1 ResolutionMultiplier=1.500000 "$@" > "$TMP/run-Settings.ini"
$CC eval "$(python3 "$IDB" restore "$TMP/run-Settings.ini")" > /dev/null
$CC eval "$(python3 "$IDB" clearshots ScreenDump_)" > /dev/null
$CC batch "open $URL" "wait 4000" > /dev/null
$CC play > /dev/null
# The script's own waits pace it; focus and quit have no browser equivalent here.
set --
while read -r command a b; do
  case "$command" in
    wait) set -- "$@" "wait $a" ;;
    move) set -- "$@" "move $a $b" ;;
    click) set -- "$@" "click $a $b" ;;
    drag) set -- "$@" "drag $a $b" ;;
    key) # SDL scancode names to the driver's key names where they differ.
      case "$a" in
        '`') a=Backquote ;;
        Return) a=Enter ;;
        Left|Right|Up|Down) a=Arrow$a ;;
        RAlt+*) a="AltRight+$(echo "${a#RAlt+}" | tr 'A-Z' 'a-z')" ;;
        LAlt+*) a="AltLeft+$(echo "${a#LAlt+}" | tr 'A-Z' 'a-z')" ;;
      esac
      set -- "$@" "key $a" ;;
    text) set -- "$@" "type $a" ;;
  esac
done < "$SCRIPT"
$CC batch "$@" > /dev/null
$CC wait 2000 > /dev/null
n=0
for key in $($CC eval "$(python3 "$IDB" listshots ScreenDump_)" | tr -d '"'); do
  n=$((n+1)); $CC getfile /ScreenShots "$key" "$OUT/browser_$(printf %02d $n).png" > /dev/null
done
for prefix in WorldDump ScenePreviewDump; do
  for key in $($CC eval "$(python3 "$IDB" listshots ${prefix}_)" | tr -d '"'); do
    $CC getfile /ScreenShots "$key" "$OUT/browser_$prefix.png" > /dev/null
  done
  $CC eval "$(python3 "$IDB" clearshots ${prefix}_)" > /dev/null
done
echo "browser: $n screenshots, dumps: $(ls "$OUT" | grep -c 'browser_[A-Z]')"
$CC eval "$(python3 "$IDB" clearshots ScreenDump_)" > /dev/null
$CC batch "open $URL" "wait 3000" > /dev/null
$CC eval "$(python3 "$IDB" restore "$TMP/Settings.ini")" > /dev/null
rm -rf "$TMP"
