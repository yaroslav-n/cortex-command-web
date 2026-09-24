// Linked using --pre-js with -lidbfs.js and FORCE_FILESYSTEM.
// The dependency keeps main() from reading settings before IndexedDB restores them.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  if (typeof window === 'undefined') return;
  Module.installPersistenceJournal();
  const dependency = 'restore-cortex-saves';
  addRunDependency(dependency);
  // These must match System::s_UserdataDirectory, s_ModDirectory and
  // s_ScreenshotDirectory exactly. The browser filesystem is case sensitive, so
  // a mount named "/Screenshots" left the engine writing screenshots and world
  // dumps into an unmounted "/ScreenShots" that vanished on reload.
  for (const path of ['/Userdata', '/Mods', '/ScreenShots']) {
    FS.mkdirTree(path);
    FS.mount(IDBFS, { autoPersist: true }, path);
  }
  FS.syncfs(true, function (error) {
    if (error) {
      Module.onAbort?.('Could not restore browser saves: ' + error);
      return; // Do not silently overwrite previously saved data after a failed restore.
    }
    let syncing = false;
    Module.flushSaves = function () {
      if (syncing) return;
      syncing = true;
      FS.syncfs(false, function (error) {
        syncing = false;
        if (error) console.error('Could not persist Cortex Command saves', error);
      });
    };
    // Resolves once everything written so far is in IndexedDB; used before the page
    // reloads to its start screen after the player quits.
    Module.persistSaves = function () {
      return new Promise(function (resolve) {
        const run = function () {
          syncing = true;
          FS.syncfs(false, function (error) {
            syncing = false;
            if (error) console.error('Could not persist Cortex Command saves', error);
            resolve();
          });
        };
        if (!syncing) return run();
        const wait = setInterval(function () {
          if (!syncing) {
            clearInterval(wait);
            run();
          }
        }, 20);
      });
    };
    setInterval(Module.flushSaves, 5000);
    document.addEventListener('visibilitychange', function () {
      if (document.visibilityState === 'hidden') Module.flushSaves();
    });
    removeRunDependency(dependency);
  });
});
