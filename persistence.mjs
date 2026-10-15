// IDBFS writes must never overlap: a change during a write requests one more write.
export function createPersistence({ sync, onError = () => {}, onSaved = () => {} }) {
  let running = null;
  let dirty = false;
  let enabled = true;
  let lastError = null;
  function flush() {
    if (!enabled) return Promise.resolve(false);
    dirty = true;
    if (running) return running;
    running = (async () => {
      while (dirty) {
        dirty = false;
        try {
          await sync();
          lastError = null;
          onSaved();
        } catch (error) {
          dirty = true;
          lastError = error;
          onError(error);
          // No automatic retry loop: keep the in-memory game running.
          return false;
        }
      }
      return true;
    })().finally(() => { running = null; });
    return running;
  }
  return {
    flush,
    disable() { enabled = false; },
    get enabled() { return enabled; },
    get error() { return lastError; }
  };
}

export function syncFileSystem(fs, populate) {
  return new Promise((resolve, reject) => {
    fs.syncfs(populate, error => error ? reject(error) : resolve());
  });
}
