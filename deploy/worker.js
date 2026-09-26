// The Worker behind https://yaroslav.au/cortex-command/ (notes/hosting.md).
//
// Cloudflare serves every file of the game as a static asset, with the isolation headers
// from _headers, and runs this code only for a path no asset has. The one such path that
// exists is the data package, cortex.data: at 52 MB it is over the 25 MiB limit for an
// asset, so deploy/deploy.sh keeps it in R2 under its SHA-256, and this code serves it
// from there. The hash comes from the deployed cortex.data.json, so the package always
// matches the cortex.js deployed with it.
//
// The page resumes a broken download with a Range request guarded by If-Range, and
// takes a 200 as "start again" (site/index.html, notes/files-and-saves.md), so the
// ranges here are exact: 206 for a satisfiable range of the same package, 200 for no
// range, a range of another package or one it cannot parse, 416 past the end.

const PACKAGE_PATH = '/cortex-command/cortex.data';
const DESCRIPTION_PATH = '/cortex-command/cortex.data.json';

const ISOLATION = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
  'Cross-Origin-Resource-Policy': 'same-origin',
};

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname !== PACKAGE_PATH) return env.ASSETS.fetch(request);
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      return plain(405, 'Method not allowed', { Allow: 'GET, HEAD' });
    }

    const description = await env.ASSETS.fetch(new URL(DESCRIPTION_PATH, url));
    if (!description.ok) return plain(500, 'cortex.data.json is missing');
    const { size, sha256 } = await description.json();
    const etag = `"${sha256}"`;
    const headers = {
      ...ISOLATION,
      'Content-Type': 'application/octet-stream',
      'Accept-Ranges': 'bytes',
      ETag: etag,
      // Revalidated on every visit, like the assets; no-transform keeps Cloudflare from
      // compressing it, since the page cannot resume a compressed download.
      'Cache-Control': 'no-cache, no-transform',
    };

    if (request.headers.get('If-None-Match') === etag) return new Response(null, { status: 304, headers });

    let range = null;
    const ifRange = request.headers.get('If-Range');
    if (request.headers.has('Range') && (ifRange === null || ifRange === etag)) {
      range = parseRange(request.headers.get('Range'), size);
      if (range === 'unsatisfiable') {
        return plain(416, 'Range not satisfiable', { ...headers, 'Content-Range': `bytes */${size}` });
      }
    }

    const key = `cortex.data/${sha256}`;
    const object = request.method === 'HEAD' ? await env.PACKAGES.head(key)
      : await env.PACKAGES.get(key, range ? { range } : undefined);
    if (!object) return plain(500, `The data package ${sha256} is not in R2`);
    if (object.size !== size) return plain(500, `The data package in R2 has ${object.size} bytes, not ${size}`);

    if (range) {
      const end = range.offset + range.length - 1;
      return new Response(object.body, {
        status: 206,
        headers: { ...headers, 'Content-Range': `bytes ${range.offset}-${end}/${size}`, 'Content-Length': String(range.length) },
      });
    }
    return new Response(object.body, { status: 200, headers: { ...headers, 'Content-Length': String(size) } });
  },
};

// One range of RFC 9110's "bytes=first-last", "bytes=first-" or "bytes=-suffix", as R2's
// { offset, length }; null for anything else, which is answered with the whole package
// as the RFC allows; 'unsatisfiable' for a range that starts past the end.
function parseRange(header, size) {
  const match = /^bytes=(\d*)-(\d*)$/.exec(header.trim());
  if (!match || (match[1] === '' && match[2] === '')) return null;
  if (match[1] === '') {
    const suffix = Number(match[2]);
    if (suffix === 0) return 'unsatisfiable';
    const length = Math.min(suffix, size);
    return { offset: size - length, length };
  }
  const first = Number(match[1]);
  if (match[2] !== '' && Number(match[2]) < first) return null;
  if (first >= size) return 'unsatisfiable';
  const last = match[2] === '' ? size - 1 : Math.min(Number(match[2]), size - 1);
  return { offset: first, length: last - first + 1 };
}

function plain(status, text, headers = {}) {
  return new Response(`${text}\n`, { status, headers: { ...ISOLATION, ...headers, 'Content-Type': 'text/plain' } });
}
