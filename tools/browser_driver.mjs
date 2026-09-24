#!/usr/bin/env node
// Drives the browser game in a real Chrome over the DevTools protocol.
//
// The desktop host's embedded browser pane only composites while a screenshot
// is being taken, so requestAnimationFrame almost never fires there and the
// engine advances a few frames per minute. A separate headless Chrome renders
// continuously, which is what actual play and timing measurements need.
//
// Chrome stays running between invocations; each command attaches to the
// existing page so a loaded game is not thrown away.

import { spawn } from 'node:child_process';
import { writeFileSync, readFileSync, mkdirSync, existsSync } from 'node:fs';
import { dirname } from 'node:path';

const PORT = Number(process.env.CORTEX_CDP_PORT || 9222);
const PROFILE = process.env.CORTEX_CDP_PROFILE || '/tmp/cortex-chrome-profile';
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
// WebGL backend. SwiftShader renders on the CPU and works anywhere; 'metal' uses
// the Mac's GPU, as Chrome does for a real player, which matters when comparing
// pixels with the native game (the two round texel-boundary ties differently).
const ANGLE = process.env.CORTEX_ANGLE || 'swiftshader';
// Chrome opens its own tabs (update notices, settings); remembering which tab
// holds the game keeps commands from landing on one of those.
const TARGET_FILE = `${PROFILE}/.cortex-target`;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function httpJson(path) {
  const response = await fetch(`http://127.0.0.1:${PORT}${path}`);
  return response.json();
}

async function browserReady(timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      await httpJson('/json/version');
      return true;
    } catch {
      await sleep(250);
    }
  }
  return false;
}

async function launch() {
  if (await browserReady(500)) return 'already running';
  mkdirSync(PROFILE, { recursive: true });
  const args = [
    '--headless=new',
    `--remote-debugging-port=${PORT}`,
    `--user-data-dir=${PROFILE}`,
    '--no-first-run',
    '--no-default-browser-check',
    '--window-size=1280,720',
    `--use-angle=${ANGLE}`,
    ...(ANGLE === 'swiftshader' ? ['--enable-unsafe-swiftshader'] : []),
    '--autoplay-policy=no-user-gesture-required',
    // A headless window is "occluded"; without these the renderer is throttled
    // and the game loop stalls exactly like it does in the embedded pane.
    '--disable-background-timer-throttling',
    '--disable-backgrounding-occluded-windows',
    '--disable-renderer-backgrounding',
    '--disable-features=CalculateNativeWinOcclusion',
    'about:blank',
  ];
  const child = spawn(CHROME, args, { detached: true, stdio: 'ignore' });
  child.unref();
  if (!(await browserReady(20000))) throw new Error('Chrome did not expose a debugging port');
  return 'launched';
}

// One connection per command keeps the driver stateless; the page itself holds
// the game, so reconnecting costs a round trip rather than a reload.
class Session {
  constructor(ws) {
    this.ws = ws;
    this.nextId = 1;
    this.pending = new Map();
    this.events = [];
    ws.addEventListener('message', (message) => {
      const data = JSON.parse(message.data);
      if (data.id && this.pending.has(data.id)) {
        const { resolve, reject } = this.pending.get(data.id);
        this.pending.delete(data.id);
        data.error ? reject(new Error(JSON.stringify(data.error))) : resolve(data.result);
      } else if (data.method) {
        this.events.push(data);
      }
    });
  }

  send(method, params = {}, sessionId = this.sessionId) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      // The timer must not hold the event loop open, or every invocation waits
      // out the full timeout before the process can exit.
      const timer = setTimeout(() => {
        if (this.pending.delete(id)) reject(new Error(`${method} timed out`));
      }, 120000);
      timer.unref?.();
      this.pending.set(id, {
        resolve: (value) => {
          clearTimeout(timer);
          resolve(value);
        },
        reject: (error) => {
          clearTimeout(timer);
          reject(error);
        },
      });
      this.ws.send(JSON.stringify(sessionId ? { id, method, params, sessionId } : { id, method, params }));
    });
  }
}

async function connect() {
  const version = await httpJson('/json/version');
  const ws = new WebSocket(version.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    ws.addEventListener('open', resolve, { once: true });
    ws.addEventListener('error', reject, { once: true });
  });
  const session = new Session(ws);
  const { targetInfos } = await session.send('Target.getTargets', {}, null);
  const pages = targetInfos.filter((target) => target.type === 'page');
  let remembered = null;
  try {
    remembered = readFileSync(TARGET_FILE, 'utf8').trim();
  } catch {}
  let page =
    pages.find((target) => target.targetId === remembered) ||
    pages.find((target) => target.url.includes('127.0.0.1')) ||
    pages.find((target) => !target.url.startsWith('chrome://') && target.url !== 'about:blank') ||
    pages.find((target) => target.url === 'about:blank');
  if (!page) {
    const created = await session.send('Target.createTarget', { url: 'about:blank' }, null);
    page = { targetId: created.targetId };
  }
  // Any other tab in front leaves the game tab hidden, and a hidden document
  // gets no animation frames, so the engine stops between commands.
  for (const other of pages) {
    if (other.targetId !== page.targetId) {
      await session.send('Target.closeTarget', { targetId: other.targetId }, null).catch(() => {});
    }
  }
  const attached = await session.send('Target.attachToTarget', { targetId: page.targetId, flatten: true }, null);
  session.sessionId = attached.sessionId;
  session.targetUrl = page.url;
  session.pageTargetId = page.targetId;
  await session.send('Runtime.enable');
  await session.send('Page.bringToFront').catch(() => {});
  return session;
}

// The game's own launcher routes every engine print through console; capturing
// into the page keeps the log readable across separate driver invocations.
const CAPTURE = `
(() => {
  if (process_env_NO_AUDIO) window.__cortexNoAudioProbe = true;
  if (window.__cortexLog) return;
  window.__cortexLog = [];
  for (const level of ['log', 'warn', 'error']) {
    const original = console[level].bind(console);
    console[level] = (...args) => {
      try {
        window.__cortexLog.push(level + ': ' + args.map(String).join(' '));
        if (window.__cortexLog.length > 4000) window.__cortexLog.shift();
      } catch {}
      return original(...args);
    };
  }
  if (window.__cortexNoAudioProbe) return;
  // Audio cannot be heard from here, so tap the engine's output node and keep a
  // running peak. A silent graph and a working one are otherwise identical.
  const super_set = Object.getOwnPropertyDescriptor(
    window.ScriptProcessorNode ? window.ScriptProcessorNode.prototype : Object.prototype,
    'onaudioprocess',
  )?.set || function (value) { this.__fallbackHandler = value; };
  const tap = (context, node) => {
    try {
      // ScriptProcessorNode runs its callback on the page's main thread, so a
      // long engine frame delays it and the graph emits a gap. Timing the
      // callbacks is the only way to see that from outside.
      const state = (window.__cortexAudio = window.__cortexAudio || {
        peak: 0, blocks: 0, nonSilent: 0, callbacks: 0, late: 0, worstLateMs: 0, bufferMs: 0,
      });
      let previous = 0;
      if (typeof ScriptProcessorNode !== 'undefined' && node instanceof ScriptProcessorNode) {
      Object.defineProperty(node, 'onaudioprocess', {
        configurable: true,
        get() {
          return this.__handler;
        },
        set(handler) {
          this.__handler = handler;
          const expectedMs = (this.bufferSize / context.sampleRate) * 1000;
          state.bufferMs = expectedMs;
          super_set.call(this, (event) => {
            const now = performance.now();
            if (previous) {
              const lateMs = now - previous - expectedMs;
              if (lateMs > expectedMs * 0.5) {
                state.late++;
                state.worstLateMs = Math.max(state.worstLateMs, lateMs);
              }
            }
            previous = now;
            state.callbacks++;
            return handler.call(this, event);
          });
        },
      });
      }
      const analyser = context.createAnalyser();
      analyser.fftSize = 2048;
      node.connect(analyser);
      const samples = new Float32Array(analyser.fftSize);
      window.__cortexAudio = window.__cortexAudio || { peak: 0, blocks: 0, nonSilent: 0 };
      setInterval(() => {
        analyser.getFloatTimeDomainData(samples);
        let peak = 0;
        for (const sample of samples) peak = Math.max(peak, Math.abs(sample));
        const state = window.__cortexAudio;
        state.blocks++;
        if (peak > 0.0005) state.nonSilent++;
        state.peak = Math.max(state.peak, peak);
        state.contextState = context.state;
        state.sampleRate = context.sampleRate;
      }, 50);
    } catch {}
  };
  const patch = (Ctor) => {
    if (!Ctor) return Ctor;
    const createScript = Ctor.prototype.createScriptProcessor;
    if (createScript) {
      Ctor.prototype.createScriptProcessor = function (...args) {
        const node = createScript.apply(this, args);
        tap(this, node);
        return node;
      };
    }
    return Ctor;
  };
  patch(window.AudioContext);
  patch(window.webkitAudioContext);

  // With an AudioWorklet there is no main-thread node to wrap, so watch for
  // anything connecting to a context's destination and observe it in parallel.
  const connect = AudioNode.prototype.connect;
  AudioNode.prototype.connect = function (target, ...rest) {
    const result = connect.call(this, target, ...rest);
    try {
      if (target && target === this.context.destination && !this.__cortexTapped) {
        this.__cortexTapped = true;
        tap(this.context, this);
      }
    } catch {}
    return result;
  };
})();`;

async function evaluate(session, expression, awaitPromise = true) {
  const result = await session.send('Runtime.evaluate', {
    expression,
    awaitPromise,
    returnByValue: true,
    userGesture: true,
  });
  if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || 'evaluation failed');
  return result.result.value;
}

async function mouse(session, type, x, y, button = 'left', clickCount = 0) {
  await session.send('Input.dispatchMouseEvent', {
    type,
    x,
    y,
    button,
    clickCount,
    buttons: type === 'mousePressed' ? 1 : 0,
  });
}

const KEYS = {
  Escape: { code: 'Escape', key: 'Escape', vk: 27 },
  Enter: { code: 'Enter', key: 'Enter', vk: 13, text: '\r' },
  Backquote: { code: 'Backquote', key: '`', vk: 192, text: '`' },
  Space: { code: 'Space', key: ' ', vk: 32, text: ' ' },
  Backspace: { code: 'Backspace', key: 'Backspace', vk: 8 },
  Tab: { code: 'Tab', key: 'Tab', vk: 9 },
  ShiftLeft: { code: 'ShiftLeft', key: 'Shift', vk: 16, location: 1 },
  AltRight: { code: 'AltRight', key: 'Alt', vk: 18, location: 2 },
  AltLeft: { code: 'AltLeft', key: 'Alt', vk: 18, location: 1 },
  ControlLeft: { code: 'ControlLeft', key: 'Control', vk: 17, location: 1 },
  ControlRight: { code: 'ControlRight', key: 'Control', vk: 17, location: 2 },
  ArrowUp: { code: 'ArrowUp', key: 'ArrowUp', vk: 38 },
  ArrowDown: { code: 'ArrowDown', key: 'ArrowDown', vk: 40 },
  ArrowLeft: { code: 'ArrowLeft', key: 'ArrowLeft', vk: 37 },
  ArrowRight: { code: 'ArrowRight', key: 'ArrowRight', vk: 39 },
};

for (let index = 1; index <= 12; index++) {
  KEYS[`F${index}`] = { code: `F${index}`, key: `F${index}`, vk: 111 + index };
}

// US layout: the unshifted and shifted character of each punctuation key.
const PUNCTUATION = [
  ['Minus', 189, '-', '_'], ['Equal', 187, '=', '+'], ['BracketLeft', 219, '[', '{'],
  ['BracketRight', 221, ']', '}'], ['Backslash', 220, '\\', '|'], ['Semicolon', 186, ';', ':'],
  ['Quote', 222, "'", '"'], ['Comma', 188, ',', '<'], ['Period', 190, '.', '>'],
  ['Slash', 191, '/', '?'], ['Backquote', 192, '`', '~'],
];
const SHIFTED_DIGITS = ')!@#$%^&*(';

function characterDescriptor(character) {
  if (character === ' ') return KEYS.Space;
  if (/^[a-z]$/.test(character)) return keyDescriptor(character);
  if (/^[A-Z]$/.test(character)) return { ...keyDescriptor(character.toLowerCase()), key: character, text: character, shift: true };
  if (/^[0-9]$/.test(character)) return keyDescriptor(character);
  const digit = SHIFTED_DIGITS.indexOf(character);
  if (digit >= 0) return { code: `Digit${digit}`, key: character, vk: 48 + digit, text: character, shift: true };
  for (const [code, vk, plain, shifted] of PUNCTUATION) {
    if (character === plain) return { code, key: plain, vk, text: plain };
    if (character === shifted) return { code, key: shifted, vk, text: shifted, shift: true };
  }
  throw new Error(`cannot type: ${character}`);
}

function keyDescriptor(name) {
  if (KEYS[name]) return KEYS[name];
  if (/^[a-z]$/i.test(name)) {
    const upper = name.toUpperCase();
    return { code: `Key${upper}`, key: name.toLowerCase(), vk: upper.charCodeAt(0), text: name.toLowerCase() };
  }
  if (/^[0-9]$/.test(name)) return { code: `Digit${name}`, key: name, vk: name.charCodeAt(0), text: name };
  throw new Error(`unknown key: ${name}`);
}

async function keyEvent(session, descriptor, down, modifiers) {
  // A key pressed with Control types nothing, as on a real keyboard.
  const text = modifiers & 2 ? undefined : descriptor.text;
  await session.send('Input.dispatchKeyEvent', {
    type: down ? (text ? 'keyDown' : 'rawKeyDown') : 'keyUp',
    windowsVirtualKeyCode: descriptor.vk,
    // No nativeVirtualKeyCode: it is the *platform's* code, and on macOS the
    // Windows numbers mean other keys (16, Shift on Windows, is kVK_ANSI_Y). Chrome
    // then believed a Y key was held that never came up and auto-repeated it
    // thousands of times a second, which froze the page after any Shift press.
    // Chrome derives the right native code from `code` when it is left out.
    code: descriptor.code,
    key: descriptor.key,
    text: down ? text : undefined,
    unmodifiedText: down ? text : undefined,
    location: descriptor.location || 0,
    modifiers,
  });
}

async function runCommand(session, command, args) {
  switch (command) {
      case 'open': {
        await session.send('Page.enable');
        const capture = CAPTURE.replace('process_env_NO_AUDIO', process.env.CORTEX_NO_AUDIO_PROBE ? 'true' : 'false');
        await session.send('Page.addScriptToEvaluateOnNewDocument', { source: capture });
        await session.send('Page.navigate', { url: args[0] });
        try {
          writeFileSync(TARGET_FILE, session.pageTargetId);
        } catch {}
        await sleep(1500);
        await evaluate(session, capture, false);
        console.log(`opened ${args[0]}`);
        break;
      }
      case 'url':
        console.log(await evaluate(session, 'location.href'));
        break;
      case 'eval':
        console.log(JSON.stringify(await evaluate(session, args.join(' '))));
        break;
      case 'play': {
        // Presses the start screen's button, as a player would, until the game runs:
        // "Download game" first when the browser does not keep the game yet, then
        // "Play" (site/index.html). Waits up to the given seconds (default 180).
        const deadline = Date.now() + Number(args[0] || 180) * 1000;
        let state = '';
        while (Date.now() < deadline) {
          state = await evaluate(session, 'document.body.dataset.state');
          if (state === 'running' || state === 'failed' || state === 'unsupported') break;
          if (state === 'offer-download' || state === 'offer-play' || state === 'ready') {
            const [x, y] = await evaluate(session, "(() => { const r = document.getElementById('start').getBoundingClientRect(); return [Math.round(r.x + r.width / 2), Math.round(r.y + r.height / 2)]; })()");
            await mouse(session, 'mouseMoved', x, y);
            await sleep(60);
            await mouse(session, 'mousePressed', x, y, 'left', 1);
            await sleep(90);
            await mouse(session, 'mouseReleased', x, y, 'left', 1);
          }
          await sleep(250);
        }
        console.log(`play: ${state}`);
        if (state !== 'running') process.exitCode = 1;
        break;
      }
      case 'audio':
        console.log(JSON.stringify(await evaluate(session, 'JSON.stringify(window.__cortexAudio||null)')));
        break;
      case 'audioreset':
        await evaluate(session, 'window.__cortexAudio && Object.assign(window.__cortexAudio, {peak:0, blocks:0, nonSilent:0, callbacks:0, late:0, worstLateMs:0}), 1');
        console.log('audio counters reset');
        break;
      case 'log': {
        const count = Number(args[0] || 40);
        const pattern = args[1];
        const lines = await evaluate(
          session,
          `JSON.stringify((window.__cortexLog||[]).filter(l=>${pattern ? `/${pattern}/.test(l)` : 'true'}).slice(-${count}))`,
        );
        for (const line of JSON.parse(lines)) console.log(line);
        break;
      }
      case 'getfile': {
        // Pull a file the engine wrote out of its persisted browser filesystem.
        const [database, path, destination] = args;
        const chunkSize = 96 * 1024;
        let offset = 0;
        const parts = [];
        for (;;) {
          const piece = await evaluate(
            session,
            `(async()=>{const db=await new Promise((res,rej)=>{const r=indexedDB.open(${JSON.stringify(database)});r.onsuccess=()=>res(r.result);r.onerror=()=>rej(r.error);});
             const rec=await new Promise(res=>{const r=db.transaction('FILE_DATA','readonly').objectStore('FILE_DATA').get(${JSON.stringify(path)});r.onsuccess=()=>res(r.result);});
             if(!rec) return null;
             const bytes=rec.contents.subarray(${offset}, ${offset} + ${chunkSize});
             let binary=''; for (const b of bytes) binary += String.fromCharCode(b);
             return btoa(binary);})()`,
          );
          if (piece === null) throw new Error(`no such file: ${path}`);
          if (!piece) break;
          parts.push(Buffer.from(piece, 'base64'));
          offset += chunkSize;
          if (parts[parts.length - 1].length < chunkSize) break;
        }
        mkdirSync(dirname(destination), { recursive: true });
        writeFileSync(destination, Buffer.concat(parts));
        console.log(`${destination} (${Buffer.concat(parts).length} bytes)`);
        break;
      }
      case 'stack': {
        // Interrupt the page's main thread and print where it is. A page that never
        // answers an evaluate is blocked synchronously, and V8 still honours a pause
        // request at a loop back-edge, so this names the loop that is spinning.
        // Wasm frames carry function names because the build keeps them (-g2).
        await session.send('Debugger.enable');
        await session.send('Debugger.pause');
        const deadline = Date.now() + 15000;
        let paused = null;
        while (!paused && Date.now() < deadline) {
          paused = session.events.find((event) => event.method === 'Debugger.paused');
          if (!paused) await sleep(100);
        }
        if (!paused) {
          console.log('page did not pause within 15 s');
        } else {
          for (const frame of paused.params.callFrames.slice(0, Number(args[0] || 40))) {
            console.log(`${frame.functionName || '(anonymous)'}  ${frame.url.split('/').pop()}:${frame.location.lineNumber}`);
          }
          await session.send('Debugger.resume').catch(() => {});
        }
        await session.send('Debugger.disable').catch(() => {});
        break;
      }
      case 'profile': {
        // Sample the page's main thread with V8's CPU profiler for a while and print
        // the functions that took the most time themselves. Wasm frames carry their
        // C++ names because the build keeps them (-g2). The raw profile is written
        // to the given path for a closer look (it opens in Chrome's DevTools).
        const milliseconds = Number(args[0] || 5000);
        const output = args[1];
        await session.send('Profiler.enable');
        await session.send('Profiler.setSamplingInterval', { interval: 200 });
        await session.send('Profiler.start');
        await sleep(milliseconds);
        const { profile } = await session.send('Profiler.stop');
        await session.send('Profiler.disable').catch(() => {});
        if (output) writeFileSync(output, JSON.stringify(profile));
        const byId = new Map(profile.nodes.map((node) => [node.id, node]));
        const selfSamples = new Map();
        for (const id of profile.samples) selfSamples.set(id, (selfSamples.get(id) || 0) + 1);
        const selfByName = new Map();
        for (const [id, count] of selfSamples) {
          const name = byId.get(id).callFrame.functionName || '(anonymous)';
          selfByName.set(name, (selfByName.get(name) || 0) + count);
        }
        const total = profile.samples.length;
        const top = [...selfByName].sort((a, b) => b[1] - a[1]).slice(0, Number(args[2] || 40));
        for (const [name, count] of top) console.log(`${(100 * count / total).toFixed(1).padStart(5)}%  ${name}`);
        console.log(`${total} samples`);
        break;
      }
      case 'resize': {
        // Change the page's viewport the way resizing the browser window would. The
        // game derives its resolution from the canvas, which fills the viewport, so
        // this is how that behaviour is exercised. `resize reset` restores the default.
        if (args[0] === 'reset') {
          await session.send('Emulation.clearDeviceMetricsOverride');
          console.log('viewport reset');
          break;
        }
        const [width, height] = args.map(Number);
        await session.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
        console.log(`viewport ${width}x${height}`);
        break;
      }
      case 'shot': {
        const path = args[0] || '/tmp/cortex-shot.png';
        // Optional clip lets small sprites and text be inspected without
        // shrinking the whole frame to fit an image budget.
        const [x, y, width, height, scale] = args.slice(1).map(Number);
        const params = { format: 'png' };
        if (Number.isFinite(width) && width > 0) {
          params.clip = { x, y, width, height, scale: Number.isFinite(scale) && scale > 0 ? scale : 1 };
          params.captureBeyondViewport = false;
        }
        const shot = await session.send('Page.captureScreenshot', params);
        mkdirSync(dirname(path), { recursive: true });
        writeFileSync(path, Buffer.from(shot.data, 'base64'));
        console.log(path);
        break;
      }
      case 'move':
        await mouse(session, 'mouseMoved', Number(args[0]), Number(args[1]));
        console.log(`moved ${args[0]},${args[1]}`);
        break;
      case 'click': {
        // A trailing hold in milliseconds covers sustained actions such as
        // firing a weapon, which need the button down across many frames.
        const [x, y, button = 'left', hold] = args;
        await mouse(session, 'mouseMoved', Number(x), Number(y));
        await sleep(60);
        await mouse(session, 'mousePressed', Number(x), Number(y), button, 1);
        await sleep(Number(hold || process.env.CORTEX_CLICK_HOLD_MS || 90));
        await mouse(session, 'mouseReleased', Number(x), Number(y), button, 1);
        console.log(`clicked ${x},${y} ${button}${hold ? ` held ${hold}ms` : ''}`);
        break;
      }
      case 'drag': {
        // A trailing button name drags with that button; the pie menu is driven
        // by holding the secondary button and moving onto a slice.
        const [x1, y1, x2, y2] = args.slice(0, 4).map(Number);
        const button = args[4] || 'left';
        const settle = Number(args[5] || 16);
        await mouse(session, 'mouseMoved', x1, y1);
        await sleep(40);
        await mouse(session, 'mousePressed', x1, y1, button, 1);
        for (let step = 1; step <= 10; step++) {
          await mouse(session, 'mouseMoved', x1 + ((x2 - x1) * step) / 10, y1 + ((y2 - y1) * step) / 10);
          await sleep(settle);
        }
        await mouse(session, 'mouseReleased', x2, y2, button, 1);
        console.log(`dragged ${x1},${y1} -> ${x2},${y2} ${button}`);
        break;
      }
      case 'key': {
        // "AltLeft+p" style chords (names from KEYS) hold the modifier across the tapped key.
        const parts = args[0].split('+');
        const target = keyDescriptor(parts.pop());
        const holds = parts.map(keyDescriptor);
        let modifiers = 0;
        for (const hold of holds) {
          modifiers |= hold.key === 'Alt' ? 1 : hold.key === 'Control' ? 2 : hold.key === 'Shift' ? 8 : 0;
          await keyEvent(session, hold, true, 0);
        }
        const holdMs = Number(args[1] || process.env.CORTEX_KEY_HOLD_MS || 90);
        await keyEvent(session, target, true, modifiers);
        await sleep(holdMs);
        await keyEvent(session, target, false, modifiers);
        for (const hold of holds.reverse()) await keyEvent(session, hold, false, 0);
        console.log(`key ${args[0]}`);
        break;
      }
      case 'type': {
        // Each character as a real keyboard reports it: physical code, virtual key
        // and text, with Shift held where a US layout needs it. Events carrying only
        // text reach the page as something no keyboard produces.
        for (const character of args.join(' ')) {
          const descriptor = characterDescriptor(character);
          if (descriptor.shift) await keyEvent(session, KEYS.ShiftLeft, true, 0);
          await keyEvent(session, descriptor, true, descriptor.shift ? 8 : 0);
          await sleep(20);
          await keyEvent(session, descriptor, false, descriptor.shift ? 8 : 0);
          if (descriptor.shift) await keyEvent(session, KEYS.ShiftLeft, false, 0);
          await sleep(20);
        }
        console.log('typed');
        break;
      }
      case 'wait':
        await sleep(Number(args[0] || 1000));
        console.log(`waited ${args[0] || 1000}ms`);
        break;
    default:
      console.error('commands: launch kill open url eval log audio audioreset getfile resize stack shot move click drag key type wait batch');
      process.exitCode = 2;
  }
}

async function main() {
  const [command, ...args] = process.argv.slice(2);
  if (command === 'launch') {
    console.log(await launch());
    return;
  }
  if (command === 'kill') {
    try {
      const session = await connect();
      await session.send('Browser.close', {}, null);
    } catch {}
    console.log('closed');
    return;
  }

  await launch();
  const session = await connect();
  try {
    if (command === 'batch') {
      // One connection for a whole interaction keeps per-step latency off the
      // critical path when the renderer is already busy.
      for (const step of args) {
        const [name, ...stepArgs] = step.split(/\s+/);
        await runCommand(session, name, stepArgs);
      }
    } else {
      await runCommand(session, command, args);
    }
  } finally {
    session.ws.close();
  }
}

main().catch((error) => {
  console.error(String(error.message || error));
  process.exitCode = 1;
});
