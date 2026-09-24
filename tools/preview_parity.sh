#!/bin/sh
# Render scene previews in the browser build and compare them with the preview
# images the original game shipped. Requires a running activity (the tutorial is
# enough) so the console and the dump hotkey are available.
#
# Usage: preview_parity.sh <output dir> <scene name>...
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC="$ROOT/tools/cc.sh"
OUT=$1
shift
mkdir -p "$OUT"

for scene in "$@"; do
  echo "=== $scene"
  "$CC" batch \
    "key Backquote" "wait 1000" \
    "type SceneMan:LoadScene('$scene', true)" "wait 400" \
    "key Enter" "wait 10000" "wait 6000" \
    "key Backquote" "wait 1000" \
    "key AltLeft+w" "wait 6000" "wait 4000" >/dev/null 2>&1

  key=$("$CC" eval "(async()=>{const db=await new Promise((res,rej)=>{const r=indexedDB.open('/ScreenShots');r.onsuccess=()=>res(r.result);r.onerror=()=>rej(r.error);});const store=db.transaction('FILE_DATA','readonly').objectStore('FILE_DATA');const keys=await new Promise(res=>{const r=store.getAllKeys();r.onsuccess=()=>res(r.result);});const p=keys.map(String).filter(k=>k.includes('ScenePreview')).sort();return p[p.length-1];})()" | tr -d '"')
  safe=$(printf '%s' "$scene" | tr ' /' '__')
  "$CC" getfile /ScreenShots "$key" "$OUT/$safe.png" >/dev/null
  shipped=$(find "${ORIGINAL_CODE:-$ROOT/../original-code}/Data" -name "$scene.preview.png" | head -1)
  if [ -n "$shipped" ]; then
    python3 "$ROOT/tools/compare_png.py" "$OUT/$safe.png" "$shipped" 24 || true
  else
    echo "no shipped preview for $scene"
  fi
done
