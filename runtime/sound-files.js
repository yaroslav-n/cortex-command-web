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
// A sound played before its file is here waits, silent, and starts when the file
// arrives; FMOD asks for that file ahead of the rest (Module.requestSoundFile,
// runtime/fmod/system.cpp). An Activity only starts once every file is here
// (Module.soundFilesComplete, ActivityMan::StartActivity), so a game always plays with
// all of its sounds. See notes/files-and-saves.md.
if (typeof window !== 'undefined') {
  (() => {
    const DIRECTORY = 'audio/';
    const CACHE = 'cortex-sounds';
    const PARALLEL = 6;
    // What a sound's file holds until it arrives: FMOD knows it (c_SoundFileOnItsWay in
    // runtime/fmod/internal.hpp), and it is not empty, which the engine would refuse.
    const ON_ITS_WAY = new TextEncoder().encode('cortex: this sound file is on its way\n');
    const byPath = new Map(); // engine path -> its download
    const downloads = new Map(); // copy's URL -> { url, bytes, paths, state: 'waiting' | 'loading' | 'here' | 'failed' }
    const order = []; // the list's order, Base.rte first
    const urgent = []; // asked for by the engine
    let active = 0;
    let remaining = 0;
    let loadedBytes = 0;
    let totalBytes = 0;
    let failures = 0;
    let started = false;
    let cache = null;

    Module.soundFilesComplete = false;

    // Small in a corner of the game while it runs; in the middle while an Activity
    // waits for the last files. Inside the game's area, so it stays in fullscreen.
    const progress = document.createElement('div');
    progress.id = 'sound-files';
    (document.getElementById('game') || document.body).appendChild(progress);
    const corner = 'left:12px;bottom:10px;padding:4px 8px;font-size:12px';
    const middle = 'left:50%;top:50%;transform:translate(-50%,-50%);padding:12px 20px;font-size:18px';
    let waitingForActivity = false;
    const showProgress = () => {
      if (Module.soundFilesComplete || !totalBytes) {
        progress.style.display = 'none';
        return;
      }
      const percent = Math.floor((100 * loadedBytes) / totalBytes);
      progress.textContent = (waitingForActivity ? 'Loading sounds before the game starts: ' : 'Loading sounds: ') + percent + '%';
      progress.style.cssText = 'position:absolute;z-index:3;border-radius:3px;background:rgba(9,11,16,.8);color:#eac557;' +
        'font-family:system-ui,sans-serif;pointer-events:none;' + (waitingForActivity ? middle : corner);
    };
    // The engine is waiting in ActivityMan::StartActivity for the last files.
    Module.showSoundFileWait = (waiting) => {
      waitingForActivity = waiting;
      showProgress();
    };

    const finish = () => {
      Module.soundFilesComplete = true;
      showProgress();
      console.log(`Sound files: ${byPath.size} here, ${failures} failed, ${(totalBytes / 1e6).toFixed(1)} MB in ${downloads.size} downloads`);
      if (failures) console.error(`Could not load ${failures} sound files; those sounds stay silent.`);
      // Drop copies an earlier version of the game left in the cache.
      if (cache) {
        const current = new Set([...downloads.keys()].map((url) => new URL(url, location.href).href));
        cache.keys().then((requests) => {
          for (const request of requests) if (!current.has(request.url)) cache.delete(request);
        }).catch(() => {});
      }
    };

    const write = (path, bytes, own) => {
      const slash = path.lastIndexOf('/');
      FS.unlink('/' + path);
      FS.createDataFile('/' + path.slice(0, slash), path.slice(slash + 1), bytes, true, true, own);
    };

    const fetchCopy = async (download) => {
      for (let attempt = 0; ; attempt++) {
        try {
          // Cache Storage keeps the copy; the HTTP cache would only keep a second one.
          const response = await fetch(download.url, cache ? { cache: 'no-store' } : undefined);
          if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
          return response;
        } catch (error) {
          if (attempt === 3) throw error;
          await new Promise((resolve) => setTimeout(resolve, 1000 * 3 ** attempt));
        }
      }
    };

    const load = async (download) => {
      let response = cache ? await cache.match(download.url).catch(() => undefined) : undefined;
      let bytes = response ? new Uint8Array(await response.arrayBuffer()) : null;
      if (!bytes || bytes.length !== download.bytes) {
        response = await fetchCopy(download);
        if (cache) cache.put(download.url, response.clone()).catch(() => {});
        bytes = new Uint8Array(await response.arrayBuffer());
      }
      if (bytes.length !== download.bytes) throw new Error(`${download.url} has ${bytes.length} bytes, the list says ${download.bytes}`);
      // The first sound owns the bytes; any other with the same contents gets a copy.
      download.paths.forEach((path, index) => write(path, index ? bytes.slice() : bytes, index === 0));
    };

    const next = () => {
      while (urgent.length) {
        const download = urgent.shift();
        if (download.state === 'waiting') return download;
      }
      while (order.length) {
        const download = order.shift();
        if (download.state === 'waiting') return download;
      }
      return null;
    };

    const pump = () => {
      for (let download; active < PARALLEL && (download = next()); ) {
        active++;
        download.state = 'loading';
        load(download)
          .then(() => {
            download.state = 'here';
          })
          .catch((error) => {
            download.state = 'failed';
            failures += download.paths.length;
            console.error(`Could not load the sound file ${download.url} (${download.paths.join(', ')}): ${error}`);
          })
          .finally(() => {
            active--;
            remaining--;
            loadedBytes += download.bytes;
            showProgress();
            pump();
          });
      }
      if (!remaining && !Module.soundFilesComplete) finish();
    };

    // FMOD asks for a file it is about to play ahead of the rest.
    Module.requestSoundFile = (path) => {
      const download = byPath.get(path);
      if (!download || download.state !== 'waiting') return;
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
              download = { url, bytes: Number(bytes), paths: [], state: 'waiting' };
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
      showProgress();
      pump();
    });
  })();
}
