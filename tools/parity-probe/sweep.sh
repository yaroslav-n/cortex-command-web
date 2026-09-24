#!/bin/sh
# Compare the original and the port on every scripted activity the game loads.
# Usage: sweep.sh [output-directory] [mission list, default missions.txt]
# Each list line is "<activity preset>|<scene preset>". The scene must be a real
# Scene preset: some activities' own SceneName is not (Signal Hunt's "Zombie Cave"
# is a terrain object; the menu makes the player pick). Refinery Assault is absent
# on purpose — Browncoats.rte/Index.ini comments its Activities.ini out.
# PROBE_SCRIPT / PROBE_FILE choose another probe (see run-original.sh); text outputs
# that differ are shown as a diff. With PROBE_SCREENSHOT=1 the two screenshots are
# compared too (imgdiff.py), and a side-by-side with a heat map is kept.
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUTDIR=${1:-/tmp/parity-probe}; mkdir -p "$OUTDIR"
LIST=${2:-$HERE/missions.txt}
EXT=${PROBE_FILE:-ParityProbe.bin}; EXT=${EXT##*.}
while IFS='|' read -r ACT SCENE; do
  SLUG=$(echo "$ACT-$SCENE" | tr -cd '[:alnum:]-')
  O="$OUTDIR/original_$SLUG.$EXT"; P="$OUTDIR/port_$SLUG.$EXT"; rm -f "$O" "$P"
  "$HERE/run-original.sh" "$ACT" "$SCENE" "$O" < /dev/null
  [ -f "$O" ] || { echo "$ACT on $SCENE: the original did not launch this way"; continue; }
  "$HERE/run-port.sh" "$ACT" "$SCENE" "$P" < /dev/null
  if [ ! -f "$P" ]; then echo "$ACT on $SCENE: the port wrote nothing"
  elif cmp -s "$O" "$P"; then echo "$ACT on $SCENE: IDENTICAL $(head -1 "$O")"
  else echo "$ACT on $SCENE: DIFFERENT"; [ "$EXT" = txt ] && diff "$O" "$P" | head -12; fi
  if [ -n "${PROBE_SCREENSHOT:-}" ] && [ -f "$O.png" ] && [ -f "$P.png" ]; then
    echo "  screen: $(python3 "$HERE/imgdiff.py" "$O.png" "$P.png" "$OUTDIR/side_$SLUG.png")"
  fi
done < "$LIST"
