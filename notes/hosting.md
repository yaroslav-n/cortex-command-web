# Hosting: yaroslav.au/cortex-command

The game is served at **https://yaroslav.au/cortex-command/**. Every push to `main`
deploys there once the build and every check have passed: the last step of
`.github/workflows/ci.yml` runs `deploy/deploy.sh`. A pull request never deploys.

## Two Workers on one domain

The domain belongs to the home page, a separate private repository
(`yaroslav-n/yaroslav.au`) served by the Cloudflare Worker `yaroslav-au` on yaroslav.au
as a Custom Domain. The game is the Worker `cortex-command` (`deploy/wrangler.jsonc`),
on two routes, `yaroslav.au/cortex-command` and `yaroslav.au/cortex-command/*`.
Cloudflare runs route Workers before a Custom Domain's, so those paths never reach the
home page, and the two deploy independently.

## What serves what

`deploy/deploy.sh` copies from `dist/` into `build/deploy/`, under the path they are
served at: `cortex-command/` holds `index.html`, `social-preview.jpg` (the picture a
shared link shows), `cortex.js`, `cortex.wasm`, `cortex.data.json` and `audio/`, next to
`_headers` (from `deploy/_headers`). Those are
the Worker's static assets, which Cloudflare answers itself without running the
Worker's code. They come with the cross-origin isolation headers from `_headers`, an
`ETag`, and `Cache-Control: public, max-age=0, must-revalidate`, the revalidation the
README asks a host for. `/cortex-command` redirects to `/cortex-command/` (307). The
test pages are not deployed. The preview picture alone is sent with
`Cross-Origin-Resource-Policy: cross-origin` instead of `same-origin`, since other sites
are meant to show it: its rule in `_headers` removes the header the `/cortex-command/*`
rule set (`! Cross-Origin-Resource-Policy`) and sets its own, where two rules setting
the same header would send both values joined by a comma.

The data package, `cortex.data`, is 52 MB, over Cloudflare's 25 MiB limit for one
asset. It lives in the R2 bucket `cortex-command` instead, under
`cortex.data/<its SHA-256>`, and `deploy/worker.js` serves `/cortex-command/cortex.data`
from there. The Worker's code runs only for requests no asset matches, and that is the
only one it answers; anything else is passed to the assets, which say 404.

- **Which package.** The Worker reads the hash from the deployed `cortex.data.json`, so
  the package it serves is always the one built with the `cortex.js` beside it. The
  deploy script refuses a `dist/` whose package does not match its `cortex.data.json`,
  and uploads the package before deploying the Worker, so no deployed Worker ever
  looks for a package that is not there yet. Earlier packages stay in R2 under their
  own hashes (one per change to the game data); nothing deletes them yet.
- **Ranges.** The page resumes a broken download with a `Range` request guarded by
  `If-Range`, and takes a 200 as "start again" ([files and saves](files-and-saves.md)),
  so the answers are exact. A single satisfiable range, with no `If-Range` or one equal
  to the `ETag` (the quoted hash), gets a 206. A range that starts past the end gets a
  416 with `Content-Range: bytes */<size>`. Anything else gets the whole package with a
  200: no range, another package's `If-Range`, several ranges, a range it cannot parse.
  A matching `If-None-Match` gets a 304, and `HEAD` is answered.
- **No compression.** `Cache-Control: no-cache, no-transform`. `no-transform` keeps
  Cloudflare from compressing the package: the page never resumes a compressed
  download, whose ranges are not the package's.
- **Headers.** `_headers` does not apply to what a Worker's code returns, so
  `worker.js` adds the three isolation headers itself.

## Credentials and limits

CI deploys with the repository secret `CLOUDFLARE_API_TOKEN`, a token from Cloudflare's
"Edit Cloudflare Workers" template (Worker scripts, routes on the yaroslav.au zone,
R2), and the variable `CLOUDFLARE_ACCOUNT_ID`. Deploying by hand needs only
`wrangler login`, then `deploy/deploy.sh`. Deploys from `main` run one at a time, in
order (the job's `concurrency` group).

All of it is on Cloudflare's free tiers. Requests for static assets are free and do not
count as Worker requests; the Worker runs once per download of the package (100,000
requests a day are free); R2 holds up to 10 GB and 10 million reads a month free, and
charges nothing for what it sends. The free plan allows 20,000 assets per Worker
version (a deploy has 2,163) of up to 25 MiB each (the largest sound is 3.8 MB).

## Trying it locally

```sh
deploy/deploy.sh --local                            # assets, and the package into local R2
wrangler dev --config deploy/wrangler.jsonc --port 8792
```

Then open http://127.0.0.1:8792/cortex-command/. Checked this way on 2026-09-26:
- **Ranges:** every answer above, checked with curl. The bytes of a range and of the
  whole package are the file's.
- **A new player:** a fresh headless Chrome profile (`tools/cc.sh`) reached "Browser
  menu: entered" with cross-origin isolation, and all 2,248 sounds arrived.
- **A returning player:** the reload read the package from Cache Storage without
  requesting it.
