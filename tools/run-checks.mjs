#!/usr/bin/env node
// Runs every automated check against the build in dist/ and reports PASS/FAIL.
//
//   node tools/run-checks.mjs [--only name,name] [--list] [--chrome PATH]
//                             [--angle swiftshader|metal] [--dist DIR] [--keep-profile] [--log]
//
// It serves dist/ itself (with the cross-origin isolation headers the game needs),
// starts its own headless Chrome with a fresh, temporary profile, and runs:
//   - the test pages: each Emscripten test program reports through
//     tests/check-report.js (window.checkResult);
//   - the Node contracts, whose output must equal tests/golden/;
//   - the game itself: its start screen downloads nothing until asked (and explains
//     itself to a browser without JSPI), Ctrl+F there opens fullscreen, its data
//     package's download goes on after the connection breaks or stops, refuses a
//     wrong package, and says why when it gives up, and the game then never starts;
//     time the page does not run is not taken for a stall; it boots to the main
//     menu with the transparency tables it has always built, every sound file it
//     fetches after the start arrives, even through a bad network, and the
//     deterministic simulation harness reproduces its recorded state hashes, with
//     the performance overlay hidden and shown; its counters and the master Lua
//     state's script timings record only while it is shown.
// --log prints each check's page output, which otherwise shows only on failure.
// Build first: ./build.sh --target checks. Exit status is 0 only if every check passed.

import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { createReadStream, existsSync, mkdtempSync, readFileSync, rmSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, normalize, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

// The fixed window the game checks run in. The simulation's random-draw counts
// depend on it (the title screen's star field is sized from the window), so the
// recorded values below are for exactly this size and a fresh profile (scale 2).
const VIEWPORT = { width: 1280, height: 720 };
// The engine makes one Lua state per logical CPU, as the original does, and seeds each
// from the simulation's random stream (LuaMan::Initialize), so the recorded simulation
// results hold for the CPU count they were recorded with. Pages are told this many,
// whatever the machine. The worker build's engine thread reads its worker's own count,
// which this does not reach: its results hold on a 12-CPU machine only.
const CPU_COUNT = 12;

const CHECKS = [
  // Test programs that exit with status 0 when they pass.
  { name: 'runtime', page: 'runtime-check.html' },
  { name: 'audio-output', page: 'audio-check.html', click: '#start' },
  { name: 'audio-mixer', page: 'audio-mixer-check.html' },
  { name: 'audio-worklet', page: 'worklet-probe.html?auto' },
  { name: 'input-queue', page: 'browser-input-queue-check.html' },
  { name: 'gui-input', page: 'gui-input-check.html' },
  { name: 'thread-wait', page: 'thread-wait-check.html' },
  { name: 'frame-readback', page: 'frame-readback-check.html' },
  { name: 'relative-mouse', page: 'relative-mouse-check.html' },
  { name: 'texture-tile', page: 'texture-tile-check.html' },
  { name: 'texture-gpu', page: 'texture-gpu-check.html' },
  { name: 'storage-race', page: 'storage-race-check.html' },
  // Contracts whose exact output is recorded.
  { name: 'random-contract', node: 'random_contract.js', golden: 'tests/golden/random_contract.txt' },
  { name: 'float-contract', node: 'float_contract.js', golden: 'tests/golden/float_contract.txt' },
  // The game.
  // Opening the page downloads none of the game: it offers to (site/index.html). Under
  // the game, a 50 px strip says that Ctrl+F opens fullscreen, which it then must, links
  // this port and the original, and links the bug report form.
  { name: 'start-screen', start: { expect: 'offer-play', label: /^Play Game$/, strip: true } },
  // A browser without JSPI is told so and downloads nothing.
  { name: 'start-screen-no-jspi', start: { expect: 'unsupported', removeJspi: true } },
  // The data package's download goes wrong the ways a network can (FAULTS, for that check
  // only). The page tries again, from where a download stopped, and when it gives up says
  // why instead of waiting for good; it keeps only a package that is whole and right.
  // ?load-timeout makes its deadlines short (site/index.html).
  {
    name: 'package-unavailable',
    game: '?load-timeout=6000',
    download: true,
    fault: 'unavailable',
    fails: /^Could not download the game data: cortex\.data: 503 Service Unavailable \(5 tries\)\. Reload the page to try again\.$/,
    verify: (requests) => requestsFor(requests, '/cortex.data').length !== 5 && `cortex.data was asked for ${requestsFor(requests, '/cortex.data').length} times, not 5`,
  },
  {
    // A server that sends the whole package whatever is asked for, and breaks off at the
    // same place every time: no attempt gets further than the first, and the page gives up.
    name: 'package-cut-always',
    game: '?load-timeout=6000',
    download: true,
    fault: 'cutAlways',
    fails: /^Could not download the game data: .+ \(5 tries\)\. Reload the page to try again\.$/,
  },
  {
    // Downloaded twice, in case the first had been resumed from another version of the file.
    name: 'package-tampered',
    game: '?load-timeout=10000',
    download: true,
    fault: 'tampered',
    fails: /^Could not download the game data: what arrived is not this version's cortex\.data \(SHA-256 [0-9a-f]{12}…, expected [0-9a-f]{12}…\)\. Reload the page to try again\.$/,
    verify: async (requests, tab) => {
      if (requestsFor(requests, '/cortex.data').length !== 2) return `cortex.data was asked for ${requestsFor(requests, '/cortex.data').length} times, not 2`;
      const kept = await tab.evaluate("caches.open('cortex-data').then((cache) => cache.keys()).then((keys) => keys.length)");
      return kept !== 0 && `${kept} packages kept`;
    },
  },
  { name: 'game-boot', game: '', expect: /^Browser menu: entered$/, timeoutMs: 180000 },
  // A returning visit takes the package from Cache Storage, where game-boot left it, and
  // does not download it.
  {
    name: 'package-kept',
    game: '',
    expect: /^Browser menu: entered$/,
    timeoutMs: 180000,
    verify: (requests) => requestsFor(requests, '/cortex.data').length && 'cortex.data was downloaded again (this check follows game-boot)',
  },
  {
    name: 'package-cut-short',
    game: '?load-timeout=9000',
    download: true,
    fault: 'cut',
    expect: /^Browser menu: entered$/,
    timeoutMs: 180000,
    verify: (requests) => resumed(requests),
  },
  {
    name: 'package-stalled',
    game: '?load-timeout=9000',
    download: true,
    fault: 'stall',
    expect: /^Browser menu: entered$/,
    timeoutMs: 180000,
    verify: (requests) => resumed(requests),
  },
  {
    // Every byte arrives but the response never ends: when the stall's deadline has passed
    // the hash decides, and nothing more is asked for (a request for the rest would get 416).
    name: 'package-unended',
    game: '?load-timeout=9000',
    download: true,
    fault: 'unended',
    expect: /^Browser menu: entered$/,
    timeoutMs: 180000,
    verify: (requests) => requestsFor(requests, '/cortex.data').length !== 1 && `cortex.data was asked for ${requestsFor(requests, '/cortex.data').length} times, not once`,
  },
  // Any step of loading that stops is noticed, not only the package's download, and so is
  // a promise that fails with nothing to handle it; once the game runs, one does not stop it.
  {
    name: 'load-stalled',
    game: '?load-timeout=5000',
    fault: 'noProgram',
    fails: /^Loading made no progress for 5 seconds\. Reload the page to try again\.$/,
  },
  {
    // The page gives up after the package was let through, while main still waits for
    // the list of sound files, which the server holds until then: when the list comes
    // after all, neither the game nor the sounds' download starts.
    name: 'load-gave-up',
    game: '?load-timeout=5000',
    fault: 'heldSoundList',
    fails: /^Loading made no progress for 5 seconds\. Reload the page to try again\.$/,
    verify: async (requests, tab) => {
      const { list, wrong } = await soundListAlone(tab, requests, 1000);
      if (wrong) return wrong;
      list.release();
      if (!(await waitFor(tab, 'Module.calledRun === true', 10000))) return 'loading did not go on once the list arrived';
      await sleep(2000);
      const state = await tab.evaluate('document.body.dataset.state');
      if (state !== 'failed') return `the game started after the page had given up (${state})`;
      // The sounds' download shows its progress as it starts, even with every file cached.
      const sounds = requests.filter((request) => request.path.startsWith('/audio/') && request.path !== '/audio/manifest.tsv');
      if (sounds.length || (await tab.evaluate("document.getElementById('sound-files').hasAttribute('style')"))) return `the sounds' download started after the page had given up (${sounds.length} files asked for)`;
      return '';
    },
  },
  {
    // Time the page does not run is not taken for a stall, as when a computer sleeps on
    // a system whose clock runs on meanwhile: once main waits only for the list of sound
    // files, which the server holds, the page's thread is kept busy for 8 s, longer than
    // the page waits for news, and then the list is sent.
    name: 'load-paused',
    game: '?load-timeout=5000',
    fault: 'heldSoundList',
    whileLoading: async (tab, requests) => {
      const { list, wrong } = await soundListAlone(tab, requests, 20000);
      if (wrong) return wrong;
      await tab.evaluate('{ const end = performance.now() + 8000; while (performance.now() < end); }');
      list.release();
      return '';
    },
    expect: /^Browser menu: entered$/,
    timeoutMs: 180000,
  },
  { name: 'load-rejection', game: '', reject: 'loading', fails: /^a promise nothing handled$/ },
  { name: 'running-rejection', game: '', reject: 'running', expect: /^Browser menu: entered$/, timeoutMs: 180000 },
  // A step that is slow but goes on is not taken for one that stopped: the program arrives
  // over twice as long as the page waits for news, as a returning player's does after an
  // update, on a slow link, while the package comes from Cache Storage (where the checks
  // before left it).
  { name: 'slow-program', game: '?load-timeout=5000', fault: 'slowProgram', expect: /^Browser menu: entered$/, timeoutMs: 180000 },
  // The page fetches the program itself (site/index.html): one that is not there is said so,
  // and one from a host that does not call it application/wasm still runs.
  { name: 'program-missing', game: '', fault: 'noProgramFile', fails: /^Could not load cortex\.wasm: 404 Not Found\. Reload the page to try again\.$/ },
  { name: 'program-untyped', game: '', fault: 'untypedProgram', expect: /^Browser menu: entered$/, timeoutMs: 180000 },
  {
    // FrameMan's 21 preset transparency tables, built at startup on the thread pool
    // and the engine thread together, must hold exactly the bytes the serial build
    // (the original's loop) made; ?perf-debug prints a hash of them.
    name: 'colour-tables',
    game: '?perf-debug',
    expect: /^Browser colour tables: 21 in \d+ ms, hash 2452950435e18642$/,
    result: /^Browser colour tables: /,
    timeoutMs: 120000,
  },
  {
    name: 'simulate-tutorial',
    game: '?simulate=600&parallel=0',
    expect: /^Simulation: 600 steps, parallel mask 0, 82 objects, state hash c2b76d23260db076, RNG 25827 draws hash 48db148cb10b56e5$/,
    timeoutMs: 300000,
  },
  {
    // Every sound file the page fetches after the start arrives (runtime/sound-files.js).
    name: 'sound-files',
    game: '',
    expect: /^Sound files: [1-9][0-9]* here, 0 failed, /,
    timeoutMs: 120000,
  },
  {
    // With the performance overlay hidden, as it is by default, only each step's total
    // is measured (PerformanceMan::StartPerformanceMeasurement).
    name: 'simulate-dummy-assault',
    game: '?simulate-activity=Dummy%20Assault%7CDummy%20Assault&simulate=300&parallel=0',
    expect: /^Simulation: 300 steps, parallel mask 0, 191 objects, state hash 1e215e18472c610a, RNG 38865 draws hash cd713471b45b3fec$/,
    alsoExpect: [/^Performance counters \(microseconds per step\): Total [1-9][0-9]*, Act AI 0, Act Travel 0, Act Update 0, Prt Travel 0, Prt Update 0, Activity 0, Scripts 0; master state scripts: [0-9]+ calls, 0 microseconds$/],
    timeoutMs: 300000,
  },
  {
    // A mission with scripts in the master Lua state, the only state whose calls are
    // timed: its invisible automover controller and one automover node, two calls a
    // step. With the overlay hidden they are counted but not timed (LuaMan.cpp).
    name: 'simulate-zero-g',
    game: '?simulate-activity=One-Man%20Army%20(Zero-G)%7CZero-G%20Battle&simulate=300&parallel=0',
    expect: /^Simulation: 300 steps, parallel mask 0, 22 objects, state hash 331fc00d0a8b544b, RNG 9443 draws hash 254be292799af753$/,
    alsoExpect: [/^Performance counters \(microseconds per step\): Total [1-9][0-9]*, Act AI 0, Act Travel 0, Act Update 0, Prt Travel 0, Prt Update 0, Activity 0, Scripts 0; master state scripts: 2 calls, 0 microseconds$/],
    timeoutMs: 300000,
  },
  {
    // The same run with the overlay shown (?perf-debug shows it from the start): every
    // counter that has something to measure records, the master state's two calls are
    // timed, and the result is the same, since measuring changes nothing in the
    // simulation. (This mission has next to no particles to update.)
    name: 'simulate-overlay-shown',
    game: '?simulate-activity=One-Man%20Army%20(Zero-G)%7CZero-G%20Battle&simulate=300&parallel=0&perf-debug',
    expect: /^Simulation: 300 steps, parallel mask 0, 22 objects, state hash 331fc00d0a8b544b, RNG 9443 draws hash 254be292799af753$/,
    alsoExpect: [/^Performance counters \(microseconds per step\): Total [1-9][0-9]*, Act AI [1-9][0-9]*, Act Travel [1-9][0-9]*, Act Update [1-9][0-9]*, Prt Travel [0-9]+, Prt Update [0-9]+, Activity [1-9][0-9]*, Scripts [1-9][0-9]*; master state scripts: 2 calls, [1-9][0-9]* microseconds$/],
    timeoutMs: 300000,
  },
  // The sound files through a bad network, each from the network rather than the cache
  // (these two empty it). The page's timeouts are shortened for them: a download that
  // receives nothing for 4 s is dropped, and the page stops waiting for missing files
  // after 10 s without any arriving (20 s and 3 minutes for players).
  {
    // One request never answered, 20 cut off, all refused (503) for 3 s part way and one
    // of those never found until the rest are here: all arrive.
    name: 'sound-files-faults',
    faults: 'network',
    game: '?sound-file-timeouts=4,180',
    timeoutMs: 120000,
  },
  {
    // Two files missing: an Activity starts after the page stops waiting, and they
    // arrive once they can be had and the browser says it is online.
    name: 'sound-files-deadline',
    faults: 'blocked',
    game: '?simulate=30&parallel=0&sound-file-timeouts=20,10',
    timeoutMs: 120000,
  },
];

const requestsFor = (requests, path) => requests.filter((request) => request.path === path);
// Waits until the list of sound files is held (FAULTS.heldSoundList) and is all main still
// waits for: Emscripten's count of those steps (runDependencies, a global of cortex.js) is
// down to 1, so the package has been let through. Gives the held request, or why not.
async function soundListAlone(tab, requests, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let list;
  while (!(list = requestsFor(requests, '/audio/manifest.tsv')[0])?.release) {
    if (Date.now() > deadline) return { wrong: 'the list of sound files was not asked for' };
    await sleep(100);
  }
  if (await waitFor(tab, 'runDependencies === 1', Math.max(deadline - Date.now(), 500))) return { list };
  return { wrong: `main waits for ${await tab.evaluate('runDependencies')} steps, not only the list of sound files` };
}
// The download that broke off went on from where it stopped: the second request for the
// package asked for the rest of the same version of it (If-Range, the first one's ETag),
// and got it.
function resumed(requests) {
  const [first, second] = requestsFor(requests, '/cortex.data');
  if (!second || second.status !== 206 || !(second.start > 0)) return `not resumed: ${JSON.stringify(requestsFor(requests, '/cortex.data'))}`;
  if (second.ifRange !== first.etag) return `resumed with If-Range ${second.ifRange}, not ${first.etag}`;
  return '';
}

function parseArguments(argv) {
  const options = { angle: 'swiftshader', dist: join(ROOT, 'dist') };
  for (let i = 0; i < argv.length; i++) {
    const argument = argv[i];
    if (argument === '--only') options.only = new Set(argv[++i].split(','));
    else if (argument === '--list') options.list = true;
    else if (argument === '--chrome') options.chrome = argv[++i];
    else if (argument === '--angle') options.angle = argv[++i];
    else if (argument === '--dist') options.dist = resolve(argv[++i]);
    else if (argument === '--keep-profile') options.keepProfile = true;
    else if (argument === '--log') options.log = true;
    else throw new Error(`unknown argument ${argument}`);
  }
  return options;
}

function findChrome(explicit) {
  const candidates = [
    explicit,
    process.env.CHROME,
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    '/usr/bin/google-chrome',
    '/usr/bin/google-chrome-stable',
    '/usr/bin/chromium',
    '/usr/bin/chromium-browser',
  ].filter(Boolean);
  const found = candidates.find((path) => existsSync(path));
  if (!found) throw new Error('Chrome not found; pass --chrome PATH or set CHROME');
  return found;
}

const CONTENT_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.wasm': 'application/wasm',
  '.json': 'application/json',
  '.data': 'application/octet-stream',
  '.png': 'image/png',
};

// The same headers serve.py sends: SharedArrayBuffer needs cross-origin isolation.
const HEADERS = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
  'Cross-Origin-Resource-Policy': 'same-origin',
  'Cache-Control': 'no-cache',
};

// Serves the build. A request for the rest of a file (Range: bytes=N-) gets it, 206, as
// from a real host, or 416 if nothing is left, unless its If-Range names another version
// of the file than the ETag says. traffic.requests records each request; traffic.fault, which a check can set, may
// answer one itself (and return true) to break the game's download the way a network
// does, in the middle of a body too, which the DevTools protocol cannot. A check can also
// have the sound files its page asks for served badly (server.soundFaults, below);
// requests from other pages, such as one an earlier check's closing tab still had under
// way, are served as usual.
function serve(directory, traffic) {
  const server = createServer((request, response) => {
    const path = normalize(decodeURIComponent(new URL(request.url, 'http://x').pathname));
    const file = join(directory, path === '/' ? 'index.html' : path);
    if (!file.startsWith(directory) || !existsSync(file) || !statSync(file).isFile()) {
      response.writeHead(404);
      response.end();
      return;
    }
    const { size, mtimeMs } = statSync(file);
    const etag = `"${Math.round(mtimeMs).toString(36)}-${size.toString(36)}"`;
    const range = /^bytes=(\d+)-$/.exec(request.headers.range || '');
    const ifRange = request.headers['if-range'];
    const start = range && (!ifRange || ifRange === etag) ? Number(range[1]) : 0;
    const served = { path, start, status: !start ? 200 : start < size ? 206 : 416, ifRange, etag };
    const earlier = traffic.requests.filter((other) => other.path === path).length;
    traffic.requests.push(served);
    if (traffic.fault?.({ path, file, size, etag, earlier, served }, response)) return;
    const faults = server.soundFaults;
    if (faults && request.headers.referer === faults.page && path.startsWith('/audio/') && path !== '/audio/manifest.tsv') {
      const headers = { 'Content-Type': CONTENT_TYPES[extname(file)] || 'application/octet-stream', 'Content-Length': size, ...HEADERS };
      if (faults.serve(path, response, file, headers)) return;
    }
    if (served.status === 416) {
      response.writeHead(416, { 'Content-Range': `bytes */${size}`, ...HEADERS }).end();
      return;
    }
    response.writeHead(served.status, {
      'Content-Type': CONTENT_TYPES[extname(file)] || 'application/octet-stream',
      'Content-Length': size - start,
      ...(start ? { 'Content-Range': `bytes ${start}-${size - 1}/${size}` } : {}),
      'Accept-Ranges': 'bytes',
      ETag: etag,
      ...HEADERS,
    });
    createReadStream(file, { start }).pipe(response);
  });
  return new Promise((done) => server.listen(0, '127.0.0.1', () => done(server)));
}

// A network that fails the sound downloads in the ways runtime/sound-files.js must
// survive: 20 cut off after their first bytes; once 300 files have been served whole,
// the cut ones among them, so that the page has nothing left to try again, every sound
// file refused (503) for 3 s, long enough for the page to go to one file at a time,
// the first of them then not found (404) until the check mends it, so that it keeps
// failing all along; and after that one request never answered. Files served whole
// are counted in `whole`.
function badNetwork() {
  const faults = { refused: 0, requests: 0, outageFrom: 0, afterOutage: 0, broken: '', brokenRefused: 0, mended: false, held: '', heldClosed: false, heldServed: false, cut: new Map(), whole: new Set() };
  const refuse = (response, headers, status) => {
    response.writeHead(status, { ...headers, 'Content-Length': 0 });
    response.end();
    return true;
  };
  faults.serve = (path, response, file, headers) => {
    if (path === faults.broken && !faults.mended) {
      faults.brokenRefused++;
      return refuse(response, headers, 404);
    }
    const now = Date.now();
    if (faults.whole.size >= 300 && faults.cut.size === 20 && [...faults.cut.keys()].every((cut) => faults.whole.has(cut))) faults.outageFrom ||= now;
    if (faults.outageFrom && now - faults.outageFrom < 3000) {
      faults.refused++;
      faults.broken ||= path;
      return refuse(response, headers, 503);
    }
    if (path === faults.held) faults.heldServed = true;
    if (faults.outageFrom && !faults.held && ++faults.afterOutage === 5) {
      faults.held = path;
      response.on('close', () => (faults.heldClosed = true));
      return true;
    }
    const request = ++faults.requests;
    if (request % 10 === 0 && faults.cut.size < 20 && !faults.cut.has(path)) {
      const bytes = readFileSync(file);
      faults.cut.set(path, bytes.length);
      response.writeHead(200, headers);
      response.write(bytes.subarray(0, Math.min(16384, bytes.length >> 1)), () => response.destroy());
      return true;
    }
    faults.whole.add(path);
    return false;
  };
  return faults;
}

// Two sound files that cannot be had (404) until the check unblocks them. Each is used
// by one sound only, so that two sounds go missing.
function blockedSoundFiles(manifest) {
  const uses = new Map();
  for (const [, , , , name] of manifest) uses.set(name, (uses.get(name) || 0) + 1);
  const blocked = new Set(manifest.filter(([, , , , name]) => uses.get(name) === 1).slice(-2).map(([, , , , name]) => '/audio/' + name));
  const faults = { blocked, refused: 0 };
  faults.serve = (path, response, file, headers) => {
    if (!blocked.has(path)) return false;
    faults.refused++;
    response.writeHead(404, { ...headers, 'Content-Length': 0 });
    response.end();
    return true;
  };
  return faults;
}

// The first bytes of a file as the start of the whole of it, then whatever `then` does.
function sendStart(response, request, bytes, then) {
  Object.assign(request.served, { start: 0, status: 200 });
  response.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': request.size, ETag: request.etag, ...HEADERS });
  response.write(readFileSync(request.file).subarray(0, bytes), then);
}

// What a check can do to the game's requests (serve).
const FAULTS = {
  // Every request for the data package fails.
  unavailable: (request, response) => {
    if (request.path !== '/cortex.data') return false;
    request.served.status = 503;
    response.writeHead(503, HEADERS).end();
    return true;
  },
  // The data package arrives with one byte changed.
  tampered: (request, response) => {
    if (request.path !== '/cortex.data' || request.served.start) return false;
    const bytes = readFileSync(request.file);
    bytes[1e6] ^= 0xff;
    response.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': bytes.length, ...HEADERS }).end(bytes);
    return true;
  },
  // The first download of the data package breaks off after 20 MB: the connection is reset.
  cut: (request, response) => request.path === '/cortex.data' && !request.earlier && (sendStart(response, request, 20e6, () => response.destroy()), true),
  // Every download of the data package breaks off after 20 MB, from a server that sends
  // all of it each time, whatever the request asks for.
  cutAlways: (request, response) => request.path === '/cortex.data' && (sendStart(response, request, 20e6, () => response.destroy()), true),
  // The first download of the data package brings every byte but never ends: it is sent
  // in chunks, and the last, empty one that would end it never is.
  unended: (request, response) => {
    if (request.path !== '/cortex.data' || request.earlier) return false;
    response.writeHead(200, { 'Content-Type': 'application/octet-stream', ETag: request.etag, ...HEADERS });
    response.write(readFileSync(request.file));
    return true;
  },
  // The first download of the data package stops after 20 MB, its connection left open.
  stall: (request, response) => request.path === '/cortex.data' && !request.earlier && (sendStart(response, request, 20e6, () => {}), true),
  // The program is never answered.
  noProgram: (request) => request.path === '/cortex.wasm',
  // The list of sound files is answered only when the check says so (served.release).
  heldSoundList: (request, response) => {
    if (request.path !== '/audio/manifest.tsv') return false;
    request.served.release = () => {
      const bytes = readFileSync(request.file);
      response.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': bytes.length, ETag: request.etag, ...HEADERS }).end(bytes);
    };
    return true;
  },
  // The program is not there.
  noProgramFile: (request, response) => {
    if (request.path !== '/cortex.wasm') return false;
    request.served.status = 404;
    response.writeHead(404, HEADERS).end();
    return true;
  },
  // The program is served as any file, not as application/wasm.
  untypedProgram: (request, response) => {
    if (request.path !== '/cortex.wasm') return false;
    const bytes = readFileSync(request.file);
    response.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': bytes.length, ...HEADERS }).end(bytes);
    return true;
  },
  // The program arrives slowly, 64 KB every 60 ms (10 s for its 10.7 MB), and none of it
  // is lost: slower than the check lets loading go without news, but never stopping.
  slowProgram: (request, response) => {
    if (request.path !== '/cortex.wasm') return false;
    const bytes = readFileSync(request.file);
    response.writeHead(200, { 'Content-Type': 'application/wasm', 'Content-Length': bytes.length, ETag: request.etag, ...HEADERS });
    let offset = 0;
    const timer = setInterval(() => {
      if (response.destroyed) clearInterval(timer);
      else if (offset >= bytes.length) clearInterval(timer), response.end();
      else response.write(bytes.subarray(offset, (offset += 65536)));
    }, 60);
    return true;
  },
};

async function launchChrome(path, angle) {
  const profile = mkdtempSync(join(tmpdir(), 'cortex-checks-'));
  const args = [
    '--headless=new',
    '--remote-debugging-port=0',
    `--user-data-dir=${profile}`,
    '--no-first-run',
    '--no-default-browser-check',
    `--window-size=${VIEWPORT.width},${VIEWPORT.height}`,
    `--use-angle=${angle}`,
    ...(angle === 'swiftshader' ? ['--enable-unsafe-swiftshader'] : []),
    '--autoplay-policy=no-user-gesture-required',
    // A headless window counts as hidden; without these Chrome throttles it and the
    // game loop, which waits for animation frames, stalls.
    '--disable-background-timer-throttling',
    '--disable-backgrounding-occluded-windows',
    '--disable-renderer-backgrounding',
    // CI machines have no sandbox support and no sound card.
    ...(process.env.CI ? ['--no-sandbox', '--disable-dev-shm-usage'] : []),
    'about:blank',
  ];
  const chrome = spawn(path, args, { stdio: 'ignore' });
  const portFile = join(profile, 'DevToolsActivePort');
  for (let waited = 0; !existsSync(portFile); waited += 100) {
    if (waited > 30000) throw new Error('Chrome did not start');
    await sleep(100);
  }
  const port = readFileSync(portFile, 'utf8').split('\n')[0].trim();
  const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
  return { chrome, profile, endpoint: version.webSocketDebuggerUrl, version: version.Browser };
}

class Connection {
  constructor(ws) {
    this.ws = ws;
    this.nextId = 1;
    this.pending = new Map();
    this.listeners = new Set();
    ws.addEventListener('message', (message) => {
      const data = JSON.parse(message.data);
      if (data.id && this.pending.has(data.id)) {
        const { done, fail } = this.pending.get(data.id);
        this.pending.delete(data.id);
        data.error ? fail(new Error(`${data.error.message} (${data.error.code})`)) : done(data.result);
      } else if (data.method) {
        for (const listener of this.listeners) listener(data);
      }
    });
  }

  static async open(url) {
    const ws = new WebSocket(url);
    await new Promise((done, fail) => {
      ws.addEventListener('open', done, { once: true });
      ws.addEventListener('error', fail, { once: true });
    });
    return new Connection(ws);
  }

  send(method, params = {}, sessionId) {
    const id = this.nextId++;
    return new Promise((done, fail) => {
      this.pending.set(id, { done, fail });
      this.ws.send(JSON.stringify(sessionId ? { id, method, params, sessionId } : { id, method, params }));
    });
  }
}

// One tab per check, closed afterwards, so no state carries over in the page. The
// profile (IndexedDB, settings) is shared by the whole run and fresh at its start.
async function withTab(connection, fn) {
  const { targetId } = await connection.send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await connection.send('Target.attachToTarget', { targetId, flatten: true });
  const lines = [];
  const times = [];
  const listener = (event) => {
    if (event.sessionId !== sessionId) return;
    if (event.method === 'Runtime.consoleAPICalled') {
      lines.push(event.params.args.map((arg) => arg.value ?? arg.description ?? '').join(' '));
      times.push(Date.now());
    } else if (event.method === 'Page.javascriptDialogOpening') {
      // The game's message boxes (alert) and assertions (confirm) would block the page
      // until the check timed out; record them and dismiss them (Cancel ignores an assertion).
      lines.push(`DIALOG ${event.params.type}: ${event.params.message.replace(/\n+/g, ' / ')}`);
      times.push(Date.now());
      connection.send('Page.handleJavaScriptDialog', { accept: event.params.type === 'alert' }, sessionId).catch(() => {});
    } else if (event.method === 'Runtime.exceptionThrown') {
      const details = event.params.exceptionDetails;
      lines.push('EXCEPTION ' + ((details.exception && details.exception.description) || details.text));
      times.push(Date.now());
    }
  };
  connection.listeners.add(listener);
  const tab = {
    lines,
    times,
    started: Date.now(),
    send: (method, params) => connection.send(method, params, sessionId),
    evaluate: async (expression) => {
      const result = await connection.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }, sessionId);
      return result.result && result.result.value;
    },
  };
  try {
    await tab.send('Runtime.enable');
    await tab.send('Page.enable');
    await tab.send('Emulation.setDeviceMetricsOverride', { ...VIEWPORT, deviceScaleFactor: 1, mobile: false });
    await tab.send('Emulation.setHardwareConcurrencyOverride', { hardwareConcurrency: CPU_COUNT });
    return await fn(tab);
  } finally {
    connection.listeners.delete(listener);
    await connection.send('Target.closeTarget', { targetId }).catch(() => {});
  }
}

async function waitFor(tab, expression, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const value = await tab.evaluate(expression).catch(() => undefined);
    if (value) return value;
    await sleep(250);
  }
  return undefined;
}

async function runPage(connection, base, check) {
  return withTab(connection, async (tab) => {
    await tab.send('Page.navigate', { url: `${base}/${check.page}` });
    if (check.click) {
      if (!(await waitFor(tab, 'window.checkReady === true', 60000))) return { status: 'fail', detail: 'page never became ready', lines: tab.lines };
      await tab.evaluate(`document.querySelector(${JSON.stringify(check.click)}).click()`);
    }
    const result = await waitFor(tab, 'window.checkResult', check.timeoutMs || 90000);
    return result ? { ...result, lines: tab.lines, times: tab.times, started: tab.started } : { status: 'fail', detail: 'no result (timed out)', lines: tab.lines };
  });
}

// The start screen alone: what it offers, and that it has fetched none of the game
// (no cortex.js, and next to nothing over the network).
async function runStartScreen(connection, base, check) {
  return withTab(connection, async (tab) => {
    if (check.start.removeJspi) await tab.send('Page.addScriptToEvaluateOnNewDocument', { source: 'delete WebAssembly.Suspending;' });
    await tab.send('Page.navigate', { url: `${base}/index.html` });
    const state = await waitFor(tab, "!['checking', undefined].includes(document.body.dataset.state) && document.body.dataset.state", 30000);
    if (state !== check.start.expect) return { status: 'fail', detail: `the start screen is "${state}", expected "${check.start.expect}"`, lines: tab.lines };
    const label = check.start.label ? await waitFor(tab, `${check.start.label}.test(document.getElementById('start').textContent) && document.getElementById('start').textContent`, 10000) : '';
    if (check.start.label && !label) return { status: 'fail', detail: `the button says "${await tab.evaluate("document.getElementById('start').textContent")}"`, lines: tab.lines };
    await sleep(2000);
    const fetched = await tab.evaluate(`JSON.stringify({ script: !!document.querySelector('script[src="cortex.js"]'),
      bytes: performance.getEntriesByType('resource').reduce((sum, entry) => sum + entry.transferSize, 0) })`);
    const { script, bytes } = JSON.parse(fetched);
    if (script || bytes > 2e6) return { status: 'fail', detail: `the start screen loaded the game (${script ? 'cortex.js, ' : ''}${(bytes / 1e6).toFixed(1)} MB)`, lines: tab.lines };
    if (check.start.strip) {
      const strip = JSON.parse(await tab.evaluate(`JSON.stringify({
        links: [...document.querySelectorAll('#bar a')].map((link) => link.hostname + link.pathname + ' ' + link.target + (link.querySelector('svg') ? ' icon' : '')),
        names: [...document.querySelectorAll('#bar a')].map((link) => link.textContent).join(' | '),
        hint: document.getElementById('fullscreen-hint').textContent,
        bar: document.getElementById('bar').getBoundingClientRect().height,
        game: document.getElementById('game').getBoundingClientRect().height,
        window: innerHeight })`));
      const links = strip.links.join(', ');
      const expected = 'github.com/yaroslav-n/cortex-command-web _blank icon, github.com/cortex-command-community/Cortex-Command-Community-Project _blank icon, '
        + 'forms.gle/xyCqE7HFgA5KzMt5A _blank icon';
      if (links !== expected || strip.names !== 'Cortex Command Web | Cortex Command Community Project | Submit a bug' ||
        strip.hint !== 'Open fullscreen by pressing Ctrl + F' ||
        strip.bar !== 50 || strip.game !== strip.window - 50) {
        return { status: 'fail', detail: `the strip under the game is not as expected: ${JSON.stringify(strip)}`, lines: tab.lines };
      }
      // Ctrl+F, pressed as a player would, gives the game's area the whole screen.
      for (const type of ['rawKeyDown', 'keyUp']) {
        await tab.send('Input.dispatchKeyEvent', { type, modifiers: 2, key: 'f', code: 'KeyF', windowsVirtualKeyCode: 70 });
      }
      if (!(await waitFor(tab, "document.fullscreenElement?.id === 'game'", 5000))) return { status: 'fail', detail: 'Ctrl+F did not open fullscreen', lines: tab.lines };
      await tab.evaluate('document.exitFullscreen()');
    }
    const text = await tab.evaluate("document.getElementById('status').textContent");
    return { status: 'pass', detail: `${label || text} (${(bytes / 1e6).toFixed(2)} MB fetched)${check.start.strip ? ', strip under the game, Ctrl+F fullscreen' : ''}`, lines: tab.lines };
  });
}

// Opens the page and presses "Play Game", which loads the game and starts it as soon as
// it has loaded (site/index.html). Returns why it did not start, if it did not.
async function play(tab, url) {
  await tab.send('Page.navigate', { url });
  const state = "document.body.dataset.state";
  if (!(await waitFor(tab, `${state} === 'offer-play'`, 60000))) return `the page offered no game (${await tab.evaluate(state)})`;
  await tab.evaluate("document.getElementById('start').click()");
  if (!(await waitFor(tab, `${state} === 'running'`, 240000))) return 'the game never started';
  return '';
}

async function runGame(connection, base, check, traffic) {
  return withTab(connection, async (tab) => {
    const navigated = Date.now();
    traffic.requests = [];
    // A promise rejected with nothing to handle it as soon as the page is in that state.
    if (check.reject) {
      await tab.send('Page.addScriptToEvaluateOnNewDocument', { source: `new MutationObserver((changes, observer) => {
        if (document.body.dataset.state !== ${JSON.stringify(check.reject)}) return;
        observer.disconnect();
        Promise.reject('a promise nothing handled');
      }).observe(document, { subtree: true, attributeFilter: ['data-state'] });` });
    }
    await tab.send('Page.navigate', { url: `${base}/index.html${check.game}` });
    // The start screen offers "Play Game", which loads the game and starts it as soon as
    // it has loaded (site/index.html).
    const state = "document.body.dataset.state";
    if (!(await waitFor(tab, `${state} === 'offer-play'`, 60000))) return { status: 'fail', detail: `the page offered no game (${await tab.evaluate(state)})`, lines: tab.lines };
    // The data package as a first visit gets it, not from an earlier check.
    if (check.download) await tab.evaluate("caches.delete('cortex-data')");
    traffic.fault = FAULTS[check.fault] || null;
    await tab.evaluate("document.getElementById('start').click()");
    // What a check does to the page while it loads.
    const interfered = await check.whileLoading?.(tab, traffic.requests);
    if (interfered) return { status: 'fail', detail: interfered, lines: tab.lines };
    const reached = await waitFor(tab, `['running', 'failed'].includes(${state}) && ${state}`, 240000);
    const reason = reached === 'failed' ? (await tab.evaluate("document.getElementById('errors').textContent")).split('\n').pop() : '';
    if (check.fails) {
      if (reached !== 'failed') return { status: 'fail', detail: reached ? 'the game started' : 'the page never gave up', lines: tab.lines };
      if (!check.fails.test(reason)) return { status: 'fail', detail: `the page says "${reason}"`, lines: tab.lines };
      const wrong = await check.verify?.(traffic.requests, tab);
      if (wrong) return { status: 'fail', detail: wrong, lines: tab.lines };
      return { status: 'pass', detail: `${reason} (after ${((Date.now() - navigated) / 1000).toFixed(1)} s)`, lines: tab.lines, times: tab.times, started: navigated };
    }
    if (reached !== 'running') return { status: 'fail', detail: `the game never started${reason ? ': ' + reason : ''}`, lines: tab.lines };
    const played = Date.now();
    const deadline = Date.now() + check.timeoutMs;
    while (Date.now() < deadline) {
      // One snapshot per poll: lines keep arriving while this awaits.
      const lines = tab.lines.slice();
      const hit = lines.find((line) => check.expect.test(line));
      if (hit) {
        // Lines the game prints before its result line must be there too.
        const missing = (check.alsoExpect || []).find((pattern) => !lines.some((line) => pattern.test(line)));
        if (missing) return { status: 'fail', detail: `no line matches ${missing}`, lines: tab.lines };
        const wrong = await check.verify?.(traffic.requests, tab);
        if (wrong) return { status: 'fail', detail: wrong, lines: tab.lines };
        const timing = `started ${((played - navigated) / 1000).toFixed(1)} s after opening, then ${((Date.now() - played) / 1000).toFixed(1)} s`;
        return { status: 'pass', detail: `${hit} (${timing})`, lines: tab.lines, times: tab.times, started: played };
      }
      // A result line that is not the expected one fails at once.
      const result = lines.find((line) => (check.result || /^Simulation: .*state hash/).test(line));
      if (result) return { status: 'fail', detail: `unexpected result: ${result}`, lines: tab.lines };
      const failed = await tab.evaluate(`${state} === 'failed'`);
      if (failed) return { status: 'fail', detail: 'the game could not start', lines: tab.lines };
      await sleep(500);
    }
    return { status: 'fail', detail: 'timed out', lines: tab.lines };
  });
}

async function waitForLine(tab, pattern, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const line = tab.lines.find((text) => pattern.test(text));
    if (line) return line;
    if (await tab.evaluate("document.body.dataset.state === 'failed'")) return undefined;
    await sleep(250);
  }
  return undefined;
}

// The sound files downloaded through a bad network (check.faults: badNetwork or
// blockedSoundFiles). Each comes from the network: the cache earlier checks filled is
// emptied first.
async function runSoundFaults(connection, base, server, dist, check) {
  const manifest = readFileSync(join(dist, 'audio', 'manifest.tsv'), 'utf8').split('\n').filter(Boolean).map((line) => line.split('\t'));
  const faults = check.faults === 'network' ? badNetwork() : blockedSoundFiles(manifest);
  faults.page = `${base}/index.html${check.game}`;
  server.soundFaults = faults;
  try {
    return await withTab(connection, async (tab) => {
      await tab.send('Storage.clearDataForOrigin', { origin: base, storageTypes: 'cache_storage' });
      const failed = await play(tab, faults.page);
      if (failed) return { status: 'fail', detail: failed, lines: tab.lines };
      const played = Date.now();
      const result = await (check.faults === 'network' ? survivesBadNetwork : startsWithSoundsMissing)(tab, faults, check, manifest);
      const detail = `${result.detail} (${((Date.now() - played) / 1000).toFixed(1)} s after Play)`;
      return { ...result, detail, lines: tab.lines, times: tab.times, started: played };
    });
  } finally {
    server.soundFaults = null;
  }
}

// Every sound file arrives, each fault was met and survived, and no copy that was cut
// off was kept in the cache. The file that keeps failing must not hold up the others:
// they all arrive before it is mended and the browser says it is online.
async function survivesBadNetwork(tab, faults, check, manifest) {
  const others = new Set(manifest.map(([, , , , name]) => name)).size - 1;
  const deadline = Date.now() + check.timeoutMs;
  while (faults.whole.size < others && Date.now() < deadline && !(await tab.evaluate("document.body.dataset.state === 'failed'"))) await sleep(250);
  if (faults.whole.size < others) return { status: 'fail', detail: `only ${faults.whole.size} of the other ${others} files arrived while ${faults.broken || 'one'} kept failing` };
  faults.mended = true;
  await tab.evaluate("dispatchEvent(new Event('online'))");
  const line = await waitForLine(tab, /^Sound files: /, 20000);
  if (!line) return { status: 'fail', detail: 'the sound files never all arrived' };
  const [, failed, tries] = /^Sound files: \d+ here, (\d+) failed, .*?(?:, (\d+) failed tries)?$/.exec(line) || [];
  const failures = (path) => tab.lines.filter((text) => text.startsWith(`Could not load the sound file ${path.slice(1)} `));
  const problems = [];
  if (failed !== '0') problems.push(`${failed} failed`);
  if (!faults.refused || !faults.brokenRefused || faults.cut.size !== 20 || !faults.held) problems.push(`the faults were not all made (${faults.refused} refused, ${faults.brokenRefused} not found, ${faults.cut.size} cut, held: ${faults.held || 'none'})`);
  if (faults.held && !(faults.heldClosed && faults.heldServed && failures(faults.held).some((text) => /nothing arrived for/.test(text)))) problems.push(`the request never answered was not dropped and made again (${faults.held})`);
  const unnoticed = [...faults.cut.keys()].filter((path) => !failures(path).length);
  if (unnoticed.length) problems.push(`cut off but taken as whole: ${unnoticed.join(', ')}`);
  if (Number(tries || 0) < faults.refused + faults.cut.size + 1) problems.push(`only ${tries || 0} failed tries counted for ${faults.refused} refused, ${faults.cut.size} cut and 1 never answered`);
  const expected = JSON.stringify([...faults.cut].map(([path, size]) => [path.slice(1), size]));
  const cached = await waitFor(tab, `(async () => {
    const cache = await caches.open('cortex-sounds');
    for (const [url, size] of ${expected}) {
      const response = await cache.match(url);
      if (!response || (await response.arrayBuffer()).byteLength !== size) return false;
    }
    return true;
  })()`, 10000);
  if (!cached) problems.push('a file cut off is not in the cache whole');
  if (problems.length) return { status: 'fail', detail: problems.join('; ') };
  return { status: 'pass', detail: `${line}; ${faults.refused} refused (503), 1 never answered and dropped, 20 cut off, 1 not found (404) ${faults.brokenRefused} times while all ${others} others arrived` };
}

// With two sound files missing, an Activity waits for them until the page stops waiting,
// then starts, and the page says how many sounds are missing; once the files can be had
// and the browser is back online, they arrive. (On a machine slow to load the game, the
// page may stop waiting before the Activity asks; it then starts at once.)
async function startsWithSoundsMissing(tab, faults, check) {
  const result = await waitForLine(tab, /^Simulation: \d+ steps, /, check.timeoutMs);
  if (!result) return { status: 'fail', detail: 'the Activity never started' };
  const at = (pattern) => tab.lines.findIndex((text) => pattern.test(text));
  const waited = at(/^Browser sounds: waiting for the last sound files/);
  const gaveUp = at(/^Sound files: \d+ here, 2 failed, /);
  const started = at(/^Simulation: \d+ steps, /);
  if (gaveUp < 0 || started < gaveUp) return { status: 'fail', detail: `the Activity did not start once the page stopped waiting for two files (lines ${gaveUp}, ${started})` };
  const shown = await tab.evaluate("document.getElementById('sound-files').textContent");
  if (shown !== '2 sounds could not be downloaded; still trying') return { status: 'fail', detail: `the page says "${shown}"` };
  const wait = waited >= 0 && waited < gaveUp ? `waited ${((tab.times[gaveUp] - tab.times[waited]) / 1000).toFixed(1)} s, then` : 'was asked for after the page had stopped waiting and';
  faults.blocked.clear();
  await tab.evaluate("dispatchEvent(new Event('online'))");
  const arrived = await waitForLine(tab, /^Sound files: \d+ here, 0 failed, /, 10000);
  if (!arrived) return { status: 'fail', detail: 'the missing files did not arrive once they could be had and the browser was online' };
  if (!(await tab.evaluate("getComputedStyle(document.getElementById('sound-files')).display === 'none'"))) return { status: 'fail', detail: 'the page still says sounds are missing' };
  return { status: 'pass', detail: `the Activity ${wait} started with "${shown}"; then ${arrived}` };
}

function runNode(dist, check) {
  return new Promise((done) => {
    const child = spawn(process.execPath, [check.node], { cwd: dist });
    let output = '';
    child.stdout.on('data', (chunk) => (output += chunk));
    child.stderr.on('data', (chunk) => (output += chunk));
    child.on('close', (code) => {
      const expected = readFileSync(join(ROOT, check.golden), 'utf8');
      if (code !== 0) return done({ status: 'fail', detail: `exit code ${code}`, lines: output.split('\n') });
      if (output !== expected) return done({ status: 'fail', detail: `output differs from ${check.golden}`, lines: output.split('\n') });
      done({ status: 'pass', detail: `matches ${check.golden}` });
    });
  });
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  const checks = CHECKS.filter((check) => !options.only || options.only.has(check.name));
  if (options.list) {
    for (const check of CHECKS) console.log(check.name);
    return 0;
  }
  if (!existsSync(join(options.dist, 'index.html'))) throw new Error(`no build in ${options.dist}; run ./build.sh --target checks`);

  const traffic = { requests: [], fault: null };
  const server = await serve(options.dist, traffic);
  const base = `http://127.0.0.1:${server.address().port}`;
  const browser = await launchChrome(findChrome(options.chrome), options.angle);
  const connection = await Connection.open(browser.endpoint);
  console.log(`${browser.version}, WebGL via ${options.angle}, serving ${options.dist}\n`);

  let failures = 0;
  try {
    for (const check of checks) {
      const started = Date.now();
      let result;
      try {
        if (check.page) result = await runPage(connection, base, check);
        else if (check.start) result = await runStartScreen(connection, base, check);
        else if (check.node) result = await runNode(options.dist, check);
        else if (check.faults) result = await runSoundFaults(connection, base, server, options.dist, check);
        else result = await runGame(connection, base, check, traffic);
      } catch (error) {
        result = { status: 'fail', detail: String(error.message || error) };
      }
      traffic.fault = null;
      const seconds = ((Date.now() - started) / 1000).toFixed(1).padStart(6);
      const pass = result.status === 'pass';
      if (!pass) failures++;
      console.log(`${pass ? 'PASS' : 'FAIL'}  ${check.name.padEnd(24)} ${seconds} s  ${result.detail || ''}`);
      if (result.lines && (options.log || !pass)) {
        // With --log, each line carries the seconds since the page (or the game) started.
        const stamp = (i) => (options.log && result.times && result.started ? `${((result.times[i] - result.started) / 1000).toFixed(2).padStart(6)} ` : '');
        const shown = result.lines.map((line, i) => `${stamp(i)}${line}`).filter((line, i) => result.lines[i]);
        for (const line of options.log ? shown : shown.slice(-25)) console.log(`        | ${line}`);
      }
    }
  } finally {
    connection.ws.close();
    browser.chrome.kill();
    server.close();
    await sleep(500);
    if (!options.keepProfile) rmSync(browser.profile, { recursive: true, force: true });
  }
  console.log(`\n${checks.length - failures} of ${checks.length} checks passed`);
  return failures ? 1 : 0;
}

main().then(
  (code) => process.exit(code),
  (error) => {
    console.error(String(error.stack || error));
    process.exit(2);
  },
);
