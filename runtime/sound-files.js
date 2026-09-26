// Linked with --pre-js. The game's sounds, 85% of its data, are not in its data
// package: they are fetched after the game has loaded, so Play is ready as soon as
// the rest has arrived.
//
// audio/manifest.tsv (tools/audio_manifest.c) lists every sound file: its path in the
// game's filesystem, size, length, sample rate, and the name of its copy in audio/,
// which is made from its contents, so a copy never changes and Cache Storage can keep
// it for good; sounds with the same contents share one copy. Before main runs the
// list is written to /audio-manifest.tsv, where FMOD reads each sound's length and
// sample rate, and every sound gets a stand-in file where the engine expects it. Then
// each copy is taken from the cache or downloaded, once, and written over the
// stand-ins of every sound that uses it.
//
// A download that fails, or receives nothing for 20 seconds, is tried again later,
// for as long as the page is open; a download that is slow but moving is never cut
// short. When every recent try has failed, as in an outage, one file at a time is
// tried, less and less often, until one arrives or the browser is back online.
//
// A sound played before its file is here waits, silent, and starts when the file
// arrives; FMOD asks for that file ahead of the rest (Module.requestSoundFile,
// runtime/fmod/system.cpp). An Activity only starts once every file is here
// (Module.soundFilesComplete, ActivityMan::StartActivity), so a game plays with all
// of its sounds, unless nothing at all has arrived for three minutes: then the page
// stops waiting, writes a marker over the stand-ins of the files still missing
// (c_SoundFileFailed in runtime/fmod/internal.hpp), which FMOD plays as silence as
// long as each sound (one that loops forever waits for its file instead), and keeps
// trying them. See notes/files-and-saves.md.
if (typeof window !== 'undefined') {
  (() => {
    const DIRECTORY = 'audio/';
    const CACHE = 'cortex-sounds';
    // Downloads under way at once. They share Chrome's one HTTP/2 connection to the host,
    // where 24 at a time moved about twice as many bytes a second as 6 on a fast link.
    const PARALLEL = 24;
    // Tries failing in a row that are taken for an outage: from then on, one file at a time.
    const OUTAGE_FAILURES = 6;
    // A download that receives nothing for this long is dropped and tried again; the
    // page stops waiting for the missing files once nothing at all has arrived for the
    // second. tools/run-checks.mjs shortens both with ?sound-file-timeouts=<s>,<s>.
    const timeouts = (new URLSearchParams(location.search).get('sound-file-timeouts') || '').split(',');
    const STALL_MS = 1000 * (Number(timeouts[0]) || 20);
    const GIVE_UP_MS = 1000 * (Number(timeouts[1]) || 180);
    // What a sound's file holds until it arrives: FMOD knows it (c_SoundFileOnItsWay in
    // runtime/fmod/internal.hpp), and it is not empty, which the engine would refuse.
    const ON_ITS_WAY = new TextEncoder().encode('cortex: this sound file is on its way\n');
    // What it holds once the page has stopped waiting for it (c_SoundFileFailed).
    const FAILED = new TextEncoder().encode('cortex: this sound file could not be downloaded\n');
    const byPath = new Map(); // engine path -> its download
    // copy's URL -> { url, bytes, paths, state: 'waiting' | 'loading' | 'here', tries, retryAt }
    const downloads = new Map();
    const order = []; // the list's order, Base.rte first
    const urgent = []; // asked for by the engine
    const retrying = new Set(); // failed at least once and not here yet, in the order they last failed
    const attempts = new Set(); // the downloads under way, with when each last received bytes
    let active = 0;
    let remaining = 0;
    let loadedBytes = 0;
    let totalBytes = 0;
    let soundsHere = 0;
    let failedTries = 0;
    let failStreak = 0; // tries that failed since a file last arrived
    let probes = 0; // tries made one at a time since then, because every recent one failed
    let nextProbeAt = 0; // when to make the next of those
    let lastArrivalAt = 0; // moved on past any time the page did not run (tick)
    let lastTickAt = 0;
    let gaveUp = false;
    let done = false;
    let started = false;
    let cache = null;
    let timer = 0;

    Module.soundFilesComplete = false;

    // Small in a corner of the game while it runs; in the middle while an Activity
    // waits for the last files. Inside the game's area, so it stays in fullscreen.
    const progress = document.createElement('div');
    progress.id = 'sound-files';
    (document.getElementById('game') || document.body).appendChild(progress);
    const corner = 'left:12px;bottom:10px;padding:4px 8px;font-size:12px';
    const middle = 'left:50%;top:50%;transform:translate(-50%,-50%);padding:12px 20px;font-size:18px';
    let waitingForActivity = false;
    const sounds = (set) => [...set].reduce((count, download) => count + download.paths.length, 0);
    const showProgress = () => {
      if (done || !totalBytes) {
        progress.style.display = 'none';
        return;
      }
      if (gaveUp) {
        const missing = byPath.size - soundsHere;
        progress.textContent = `${missing} sound${missing === 1 ? '' : 's'} could not be downloaded; still trying`;
      } else {
        const percent = Math.floor((100 * loadedBytes) / totalBytes);
        const retries = sounds(retrying);
        progress.textContent = (waitingForActivity ? 'Loading sounds before the game starts: ' : 'Loading sounds: ') + percent + '%' +
          (retries ? `, ${retries} retrying` : '');
      }
      progress.style.cssText = 'position:absolute;z-index:3;border-radius:3px;background:rgba(9,11,16,.8);color:#eac557;' +
        'font-family:system-ui,sans-serif;pointer-events:none;' + (waitingForActivity && !gaveUp ? middle : corner);
    };
    // The engine is waiting in ActivityMan::StartActivity for the last files.
    Module.showSoundFileWait = (waiting) => {
      waitingForActivity = waiting;
      showProgress();
    };

    const report = () => {
      const tried = failedTries ? `, ${failedTries} failed tries` : '';
      console.log(`Sound files: ${soundsHere} here, ${byPath.size - soundsHere} failed, ${(totalBytes / 1e6).toFixed(1)} MB in ${downloads.size} downloads${tried}`);
    };

    const finish = () => {
      done = true;
      clearInterval(timer);
      Module.soundFilesComplete = true;
      showProgress();
      report();
      // Drop copies an earlier version of the game left in the cache.
      if (cache) {
        const current = new Set([...downloads.keys()].map((url) => new URL(url, location.href).href));
        cache.keys().then((requests) => {
          for (const request of requests) if (!current.has(request.url)) cache.delete(request);
        }).catch(() => {});
      }
    };

    // A new file in place of the old, so that a read of the old one under way (from the
    // engine's thread, in the worker build) finishes with the old contents.
    const write = (path, bytes, own) => {
      const slash = path.lastIndexOf('/');
      FS.unlink('/' + path);
      FS.createDataFile('/' + path.slice(0, slash), path.slice(slash + 1), bytes, true, true, own);
    };

    // Nothing has arrived for GIVE_UP_MS: an Activity need not wait for the rest. Their
    // sounds play as silence of their own length until they arrive, which is still tried.
    const giveUp = () => {
      gaveUp = true;
      Module.soundFilesComplete = true;
      for (const download of downloads.values()) {
        if (download.state !== 'here') for (const path of download.paths) write(path, FAILED, false);
      }
      report();
      console.error(`Could not download ${byPath.size - soundsHere} sound files; those sounds are silent until they arrive, which is still being tried.`);
      showProgress();
    };

    // The cached copy, if it is there whole; one that cannot be read or has the wrong
    // length is dropped.
    const fromCache = async (download) => {
      const response = cache && (await cache.match(download.url).catch(() => undefined));
      if (!response) return null;
      const bytes = await response.arrayBuffer().then((buffer) => new Uint8Array(buffer), () => null);
      if (bytes && bytes.length === download.bytes) return bytes;
      await cache.delete(download.url).catch(() => {});
      return null;
    };

    const fromNetwork = async (download) => {
      const attempt = { lastBytesAt: 0 };
      const controller = new AbortController();
      let watchdog = 0;
      const watch = () => {
        clearTimeout(watchdog);
        watchdog = setTimeout(() => controller.abort(new Error(`nothing arrived for ${STALL_MS / 1000} s`)), STALL_MS);
      };
      attempts.add(attempt);
      watch();
      try {
        // Cache Storage keeps the copy; the HTTP cache would only keep a second one.
        const response = await fetch(download.url, { signal: controller.signal, ...(cache && { cache: 'no-store' }) });
        if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
        // Read into one array of the listed size, which the file then owns.
        const bytes = new Uint8Array(download.bytes);
        const reader = response.body.getReader();
        for (let length = 0; ; ) {
          watch();
          const { done: end, value } = await reader.read();
          if (end) {
            if (length !== bytes.length) throw new Error(`${length} bytes arrived, the list says ${download.bytes}`);
            break;
          }
          if (length + value.length > bytes.length) throw new Error(`more than the ${download.bytes} bytes the list says`);
          bytes.set(value, length);
          length += value.length;
          attempt.lastBytesAt = performance.now();
        }
        // Only a whole copy is kept.
        if (cache) cache.put(download.url, new Response(bytes)).catch(() => {});
        return bytes;
      } finally {
        clearTimeout(watchdog);
        controller.abort(); // ends a request that failed part way
        attempts.delete(attempt);
      }
    };

    const load = async (download) => {
      const bytes = (await fromCache(download)) || (await fromNetwork(download));
      // The first sound owns the bytes; any other with the same contents gets a copy.
      download.paths.forEach((path, index) => write(path, index ? bytes.slice() : bytes, index === 0));
    };

    // The next download to try: one the engine asked for, then a retry that is due (the
    // one that failed longest ago first), then the rest in the list's order. While every
    // recent try has failed, one try is made at a time, when the next is due, to find out
    // whether downloads work again: a file not tried yet, or else the retry that failed
    // longest ago, due or not, so that a file that always fails cannot take every turn.
    // A file the engine asked for goes at once even then.
    const next = (now, probing) => {
      while (urgent.length) {
        const download = urgent.shift();
        if (download.state === 'waiting') return download;
      }
      if (probing && now < nextProbeAt) return null;
      if (!probing) {
        for (const download of retrying) if (download.state === 'waiting' && download.retryAt <= now) return download;
      }
      while (order.length) {
        const download = order.shift();
        if (download.state === 'waiting' && !download.tries) return download;
      }
      if (probing) {
        for (const download of retrying) if (download.state === 'waiting') return download;
      }
      return null;
    };

    const pump = () => {
      const now = performance.now();
      const probing = failStreak >= OUTAGE_FAILURES;
      for (let download; active < (probing ? 1 : PARALLEL) && (download = next(now, probing)); ) {
        if (probing) probes++;
        active++;
        download.state = 'loading';
        load(download)
          .then(() => {
            download.state = 'here';
            retrying.delete(download);
            remaining--;
            loadedBytes += download.bytes;
            soundsHere += download.paths.length;
            failStreak = probes = 0;
            lastArrivalAt = performance.now();
          })
          .catch((error) => {
            download.state = 'waiting';
            download.tries++;
            const delay = Math.min(60, 3 ** (download.tries - 1));
            download.retryAt = performance.now() + 1000 * delay;
            // To the end: the set is in the order the files last failed.
            retrying.delete(download);
            retrying.add(download);
            failedTries++;
            if (++failStreak >= OUTAGE_FAILURES) nextProbeAt = performance.now() + 1000 * Math.min(60, 3 ** probes);
            console.warn(`Could not load the sound file ${download.url} (${download.paths.join(', ')}): ${error}; trying again in ${delay} s`);
          })
          .finally(() => {
            active--;
            showProgress();
            pump();
          });
      }
      if (!remaining && !done) finish();
    };

    // Once a second while files are missing: retries that are due, and the deadline,
    // which counts from the last bytes of a download still under way or the last arrival.
    // A hidden tab's timers can be held back for up to a minute, so ticks can be that far
    // apart; a longer gap means the page did not run, as while the computer slept, and
    // counts as a minute.
    const tick = () => {
      const now = performance.now();
      const lastProgress = () => Math.max(lastArrivalAt, ...[...attempts].map((attempt) => attempt.lastBytesAt));
      const late = now - lastTickAt - 60000;
      if (late > 0) lastArrivalAt = Math.min(now, lastProgress() + late);
      lastTickAt = now;
      pump();
      if (!gaveUp && remaining && now - lastProgress() >= GIVE_UP_MS) giveUp();
    };

    // The browser is back online: everything waiting is tried again at once.
    window.addEventListener('online', () => {
      for (const download of retrying) download.retryAt = 0;
      failStreak = probes = 0;
      if (started && !done) pump();
    });

    // FMOD asks for a file it is about to play ahead of the rest.
    Module.requestSoundFile = (path) => {
      const download = byPath.get(path);
      if (!download || download.state !== 'waiting') return;
      download.retryAt = 0;
      urgent.push(download);
      if (started) pump();
    };

    Module.preRun = Module.preRun || [];
    Module.preRun.push(() => {
      addRunDependency('sound-file-list');
      fetch(DIRECTORY + 'manifest.tsv', { cache: 'no-cache' })
        .then((response) => {
          if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
          return response.text();
        })
        .then((list) => {
          FS.writeFile('/audio-manifest.tsv', list);
          for (const line of list.split('\n')) {
            const [path, bytes, , , name] = line.split('\t');
            if (!name) continue;
            const url = DIRECTORY + name;
            let download = downloads.get(url);
            if (!download) {
              download = { url, bytes: Number(bytes), paths: [], state: 'waiting', tries: 0, retryAt: 0 };
              downloads.set(url, download);
              order.push(download);
              remaining++;
              totalBytes += download.bytes;
            }
            download.paths.push(path);
            byPath.set(path, download);
            FS.mkdirTree('/' + path.slice(0, path.lastIndexOf('/')));
            FS.writeFile('/' + path, ON_ITS_WAY);
          }
          removeRunDependency('sound-file-list');
        })
        .catch((error) => Module.onAbort?.('Could not load the list of sound files: ' + error));
    });
    // The game has loaded: fetch the sounds now, so they do not compete with it.
    Module.postRun = Module.postRun || [];
    Module.postRun.push(async () => {
      cache = typeof caches === 'undefined' ? null : await caches.open(CACHE).catch(() => null);
      started = true;
      lastArrivalAt = lastTickAt = performance.now();
      timer = setInterval(tick, 1000);
      showProgress();
      pump();
    });
  })();
}
