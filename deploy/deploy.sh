#!/bin/sh
# Deploys the game in dist/ to https://yaroslav.au/cortex-command/ (notes/hosting.md).
#
#   deploy/deploy.sh           the data package to R2, then the Worker and its assets
#   deploy/deploy.sh --local   the same into Wrangler's local storage only, for
#                              `wrangler dev --config deploy/wrangler.jsonc`
#
# CI runs it on main once every check has passed, with CLOUDFLARE_API_TOKEN and
# CLOUDFLARE_ACCOUNT_ID set; by hand, `wrangler login` is enough. WRANGLER overrides the
# command (default: the pinned version, through npx).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST=${DIST:-$ROOT/dist}
OUT=$ROOT/build/deploy
CONFIG=$ROOT/deploy/wrangler.jsonc
WRANGLER=${WRANGLER:-npx --yes wrangler@4.141.0}
MODE=--remote
[ "${1:-}" = --local ] && MODE=--local

# The Worker finds the package by the hash in cortex.data.json, so the two must be of
# one build.
SHA=$(sed -n 's/.*"sha256": "\([0-9a-f]*\)".*/\1/p' "$DIST/cortex.data.json")
if command -v sha256sum >/dev/null; then ACTUAL=$(sha256sum "$DIST/cortex.data"); else ACTUAL=$(shasum -a 256 "$DIST/cortex.data"); fi
if [ -z "$SHA" ] || [ "$SHA" != "${ACTUAL%% *}" ]; then
  echo "deploy: $DIST/cortex.data does not match $DIST/cortex.data.json" >&2
  exit 1
fi

# The assets: the page, the program, the package's description and the sounds, under
# the path they are served at. Not the package, which is in R2, nor the test pages.
rm -rf "$OUT"
mkdir -p "$OUT/cortex-command"
cp "$DIST/index.html" "$DIST/cortex.js" "$DIST/cortex.wasm" "$DIST/cortex.data.json" "$OUT/cortex-command/"
cp -R "$DIST/audio" "$OUT/cortex-command/audio"
cp "$ROOT/deploy/_headers" "$OUT/_headers"

# The package before the Worker, so a deployed Worker never looks for one that is not
# there yet. An earlier build's package stays, under its own hash.
$WRANGLER r2 object put "cortex-command/cortex.data/$SHA" --file "$DIST/cortex.data" \
  --content-type application/octet-stream --config "$CONFIG" $MODE
if [ "$MODE" = --local ]; then exit 0; fi
$WRANGLER deploy --config "$CONFIG"
