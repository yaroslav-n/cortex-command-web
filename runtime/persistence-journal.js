// Install before mounting IDBFS. Commit only mutations made by this runtime.
Module.installPersistenceJournal = function () {
  if (IDBFS.cortexJournalInstalled) return;
  IDBFS.cortexJournalInstalled = true;
  const mountOriginal = IDBFS.mount;
  const syncOriginal = IDBFS.syncfs;
  const states = new WeakMap();
  IDBFS.mount = function (mount) {
    const auto = mount.opts.autoPersist;
    mount.opts.autoPersist = false;
    let root;
    try { root = mountOriginal(mount); } finally { mount.opts.autoPersist = auto; }
    const state = { restoring: false, sequence: 0, dirty: new Map(), queue: [], busy: false };
    states.set(mount, state);
    const mark = (path, directory = false) => {
      if (state.restoring) return;
      state.dirty.set(path, { version: ++state.sequence, directory });
      if (auto) IDBFS.queuePersist(mount);
    };
    const paths = node => {
      const result = [[FS.getPath(node), FS.isDir(node.mode)]];
      if (FS.isDir(node.mode)) for (const child of Object.values(node.contents)) result.push(...paths(child));
      return result;
    };
    const wrap = node => {
      const ops = node.node_ops;
      node.node_ops = { ...ops };
      if (ops.mknod) node.node_ops.mknod = function (...args) {
        const child = ops.mknod(...args); wrap(child); mark(FS.getPath(child), FS.isDir(child.mode)); return child;
      };
      if (ops.symlink) node.node_ops.symlink = function (...args) {
        const child = ops.symlink(...args); wrap(child); mark(FS.getPath(child)); return child;
      };
      if (ops.setattr) node.node_ops.setattr = function (target, attr) {
        const result = ops.setattr(target, attr); mark(FS.getPath(target), FS.isDir(target.mode)); return result;
      };
      for (const operation of ['unlink', 'rmdir']) if (ops[operation]) node.node_ops[operation] = function (parent, name) {
        const path = FS.getPath(parent) + '/' + name;
        const result = ops[operation](parent, name); mark(path, operation === 'rmdir'); return result;
      };
      if (ops.rename) node.node_ops.rename = function (target, parent, name) {
        const old = paths(target);
        const result = ops.rename(target, parent, name);
        // MEMFS updates parent/name after this node operation returns. Compute new paths explicitly.
        const destination = FS.getPath(parent) + '/' + name;
        for (const [path, directory] of old) { mark(path, directory); mark(destination + path.slice(old[0][0].length), directory); }
        return result;
      };
      const stream = node.stream_ops;
      node.stream_ops = { ...stream };
      for (const operation of ['write', 'allocate', 'msync']) if (stream[operation]) node.stream_ops[operation] = function (...args) {
        const result = stream[operation](...args); mark(FS.getPath(args[0].node)); return result;
      };
    };
    wrap(root);
    return root;
  };
  IDBFS.syncfs = function (mount, populate, callback) {
    const state = states.get(mount);
    if (!state) return syncOriginal(mount, populate, callback);
    state.queue.push({ populate, callback });
    const pump = () => {
      if (state.busy || !state.queue.length) return;
      state.busy = true;
      const job = state.queue.shift();
      const done = error => { state.busy = false; try { job.callback(error); } finally { pump(); } };
      if (job.populate) {
        if (state.dirty.size) return done(new Error('Cannot restore over uncommitted local files'));
        state.restoring = true;
        return syncOriginal(mount, true, error => { state.restoring = false; done(error); });
      }
      const changes = new Map(state.dirty);
      if (!changes.size) return done(null);
      const entries = new Map();
      const parents = new Map();
      try {
        for (const [path] of changes) {
          if (!FS.analyzePath(path).exists) { entries.set(path, null); continue; }
          IDBFS.loadLocalEntry(path, (error, entry) => {
            if (error) throw error;
            if (entry.contents) entry.contents = entry.contents.slice();
            entries.set(path, entry);
          });
          let parent = path.slice(0, path.lastIndexOf('/'));
          while (parent.length > mount.mountpoint.length) {
            if (!parents.has(parent)) IDBFS.loadLocalEntry(parent, (error, entry) => { if (error) throw error; parents.set(parent, entry); });
            parent = parent.slice(0, parent.lastIndexOf('/'));
          }
        }
      } catch (error) { return done(error); }
      IDBFS.getDB(mount.mountpoint, (error, db) => {
        if (error) return done(error);
        let transaction;
        try { transaction = db.transaction([IDBFS.DB_STORE_NAME], 'readwrite'); } catch (error) { return done(error); }
        const store = transaction.objectStore(IDBFS.DB_STORE_NAME);
        let finished = false;
        const fail = error => { if (!finished) { finished = true; done(error); } };
        transaction.onerror = event => { event.preventDefault(); };
        transaction.onabort = () => fail(transaction.error || new Error('Persistence transaction aborted'));
        transaction.oncomplete = () => {
          if (finished) return; finished = true;
          for (const [path, change] of changes) if (state.dirty.get(path)?.version === change.version) state.dirty.delete(path);
          done(null);
        };
        const keysRequest = store.getAllKeys();
        keysRequest.onsuccess = () => {
          try {
            const keys = keysRequest.result;
            for (const [path, entry] of parents) {
              const request = store.get(path);
              request.onsuccess = () => { try { if (!request.result) store.put(entry, path); } catch (error) { transaction.abort(); } };
            }
            for (const [path, entry] of entries) {
              if (entry) store.put(entry, path);
              else {
                // A stale local rmdir must not orphan children added by another runtime.
                const remoteChildRemains = changes.get(path).directory && keys.some(key => key.startsWith(path + '/') && entries.get(key) !== null);
                if (!remoteChildRemains) store.delete(path);
              }
            }
          } catch (error) { transaction.abort(); }
        };
      });
    };
    pump();
  };
};
