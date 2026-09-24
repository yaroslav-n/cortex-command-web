// Check that reloading the game in one tab releases the previous page.
// Usage: node reload-leak.mjs <cdp-port> <game url> [loads] [play]
// With "play" it starts the game after each load and waits PLAY_WAIT ms (default
// 12000) before measuring, so the game's main loop has started and suspended.
// Without it the page only shows its start screen, which loads nothing of the game.
// Each line reports V8's heap after a forced full collection; "backing stores"
// (ArrayBuffers, which include the 350 MB of game files) must stay flat. It grew
// by the whole previous page per reload while a suspended WebAssembly stack kept
// it alive; see notes/threads.md.
const port = process.argv[2]; const url = process.argv[3]; const loads = Number(process.argv[4] || 4); const play = process.argv[5] === 'play';
const page = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find((t) => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl); let id = 0; const pending = new Map();
ws.onmessage = (m) => { const d = JSON.parse(m.data); if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); } };
const send = (method, params = {}) => new Promise((resolve) => { const n = ++id; pending.set(n, resolve); ws.send(JSON.stringify({ id: n, method, params })); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
await new Promise((r) => (ws.onopen = r));
await send('Page.enable'); await send('HeapProfiler.enable');
for (let i = 1; i <= loads; i++) {
  if (i === 1) await send('Page.navigate', { url }); else await send('Page.reload', { ignoreCache: false });
  await sleep(6000);
  if (play) {
    // Press the start screen's "Play Game" until the game runs.
    for (let tries = 0; tries < 600; tries++) {
      const state = (await send('Runtime.evaluate', { expression: 'document.body.dataset.state', returnByValue: true })).result?.result?.value;
      if (state === 'running') break;
      if (state === 'offer-play') await send('Runtime.evaluate', { expression: "document.getElementById('start').click()", userGesture: true });
      await sleep(250);
    }
    await sleep(Number(process.env.PLAY_WAIT || 12000));
  }
  await send('HeapProfiler.collectGarbage'); await sleep(500); await send('HeapProfiler.collectGarbage');
  const u = (await send('Runtime.getHeapUsage')).result;
  const mb = (x) => x === undefined ? '-' : Math.round(x / 1048576);
  console.log(`load ${i}${play ? ' (played)' : ''}: used ${mb(u.usedSize)} MB, total ${mb(u.totalSize)} MB, embedder ${mb(u.embedderHeapUsedSize)} MB, backing stores ${mb(u.backingStorageSize)} MB`);
}
process.exit(0);
