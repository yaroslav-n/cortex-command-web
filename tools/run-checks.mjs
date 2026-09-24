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
//     itself to a browser without JSPI), it boots to the main menu, every sound file
//     it fetches after the start arrives, and the deterministic simulation harness
//     reproduces its recorded state hashes.
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
  { name: 'texture-tile', page: 'texture-tile-check.html' },
  { name: 'texture-gpu', page: 'texture-gpu-check.html' },
  { name: 'storage-race', page: 'storage-race-check.html' },
  // Contracts whose exact output is recorded.
  { name: 'random-contract', node: 'random_contract.js', golden: 'tests/golden/random_contract.txt' },
  { name: 'float-contract', node: 'float_contract.js', golden: 'tests/golden/float_contract.txt' },
  // The game.
  // Opening the page downloads none of the game: it offers to (site/index.html). Under
  // the game, a 50 px strip holds links to this port and the original, and Fullscreen.
  { name: 'start-screen', start: { expect: 'offer-download', label: /^Download game \(\d+ MB\)$/, strip: true } },
  // A browser without JSPI is told so and downloads nothing.
  { name: 'start-screen-no-jspi', start: { expect: 'unsupported', removeJspi: true } },
  { name: 'game-boot', game: '', expect: /^Browser menu: entered$/, timeoutMs: 180000 },
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
    name: 'simulate-dummy-assault',
    game: '?simulate-activity=Dummy%20Assault%7CDummy%20Assault&simulate=300&parallel=0',
    expect: /^Simulation: 300 steps, parallel mask 0, 191 objects, state hash 1e215e18472c610a, RNG 38865 draws hash cd713471b45b3fec$/,
    timeoutMs: 300000,
  },
];

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
function serve(directory) {
  const server = createServer((request, response) => {
    const path = normalize(decodeURIComponent(new URL(request.url, 'http://x').pathname));
    const file = join(directory, path === '/' ? 'index.html' : path);
    if (!file.startsWith(directory) || !existsSync(file) || !statSync(file).isFile()) {
      response.writeHead(404);
      response.end();
      return;
    }
    response.writeHead(200, {
      'Content-Type': CONTENT_TYPES[extname(file)] || 'application/octet-stream',
      'Content-Length': statSync(file).size,
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cross-Origin-Resource-Policy': 'same-origin',
      'Cache-Control': 'no-cache',
    });
    createReadStream(file).pipe(response);
  });
  return new Promise((done) => server.listen(0, '127.0.0.1', () => done(server)));
}

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
        links: [...document.querySelectorAll('#bar a')].map((link) => link.hostname + ' ' + link.target),
        button: document.getElementById('fullscreen').textContent,
        bar: document.getElementById('bar').getBoundingClientRect().height,
        game: document.getElementById('game').getBoundingClientRect().height,
        window: innerHeight })`));
      const links = strip.links.join(', ');
      if (links !== 'github.com _blank, github.com _blank' || strip.button !== 'Fullscreen' || strip.bar !== 50 || strip.game !== strip.window - 50) {
        return { status: 'fail', detail: `the strip under the game is not as expected: ${JSON.stringify(strip)}`, lines: tab.lines };
      }
    }
    const text = await tab.evaluate("document.getElementById('status').textContent");
    return { status: 'pass', detail: `${label || text} (${(bytes / 1e6).toFixed(2)} MB fetched)${check.start.strip ? ', strip under the game' : ''}`, lines: tab.lines };
  });
}

async function runGame(connection, base, check) {
  return withTab(connection, async (tab) => {
    const navigated = Date.now();
    await tab.send('Page.navigate', { url: `${base}/index.html${check.game}` });
    // The start screen offers "Download game" in a fresh profile, then "Play" once the
    // game is loaded; when the browser already keeps the game it offers "Play" at once
    // and starts as soon as it has loaded (site/index.html).
    const state = "document.body.dataset.state";
    const offered = await waitFor(tab, `['offer-download', 'offer-play'].includes(${state}) && ${state}`, 60000);
    if (!offered) return { status: 'fail', detail: `the page offered no game (${await tab.evaluate(state)})`, lines: tab.lines };
    await tab.evaluate("document.getElementById('start').click()");
    if (offered === 'offer-download') {
      if (!(await waitFor(tab, `${state} === 'ready'`, 120000))) return { status: 'fail', detail: 'the game never finished loading', lines: tab.lines };
      await tab.evaluate("document.getElementById('start').click()");
    }
    if (!(await waitFor(tab, `${state} === 'running'`, 120000))) return { status: 'fail', detail: 'the game never started', lines: tab.lines };
    const played = Date.now();
    const deadline = Date.now() + check.timeoutMs;
    while (Date.now() < deadline) {
      // One snapshot per poll: lines keep arriving while this awaits.
      const lines = tab.lines.slice();
      const hit = lines.find((line) => check.expect.test(line));
      if (hit) {
        const timing = `started ${((played - navigated) / 1000).toFixed(1)} s after opening, then ${((Date.now() - played) / 1000).toFixed(1)} s`;
        return { status: 'pass', detail: `${hit} (${timing})`, lines: tab.lines, times: tab.times, started: played };
      }
      const simulated = lines.find((line) => line.startsWith('Simulation: ') && /state hash/.test(line));
      if (simulated) return { status: 'fail', detail: `unexpected result: ${simulated}`, lines: tab.lines };
      const failed = await tab.evaluate(`${state} === 'failed'`);
      if (failed) return { status: 'fail', detail: 'the game could not start', lines: tab.lines };
      await sleep(500);
    }
    return { status: 'fail', detail: 'timed out', lines: tab.lines };
  });
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

  const server = await serve(options.dist);
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
        else result = await runGame(connection, base, check);
      } catch (error) {
        result = { status: 'fail', detail: String(error.message || error) };
      }
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
