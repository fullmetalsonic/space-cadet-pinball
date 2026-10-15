import { createPersistence, syncFileSystem } from './persistence.mjs';
import { createCanvasResizer } from './layout.mjs';
import { strings } from './locale.mjs';
import { createNameInput } from './name-input.mjs';

const byId = id => document.getElementById(id);
const canvas = byId('canvas');
const startup = byId('startup');
const status = byId('status');
const notice = byId('notice');
let ready = false;
let mounted = false;
let restoreReleased = false;
let waitingWorker = null;
let updating = false;
let updateDeferred = false;
let savingError = false;
let offlineReady = false;
let language = 'ko';
let noticeKey = null;
let statusKey = 'loading';
let startupKey = 'startup';
const t = key => strings[language][key];
function setStatus(key) { statusKey = key; status.textContent = t(key); }
function setStartup(key) { startupKey = key; byId('startup-text').textContent = t(key); }
function refreshLanguage() {
  const next = callCore('platform_is_english') ? 'en' : 'ko';
  if (next === language) return;
  language = next;
  document.documentElement.lang = language;
  document.title = t('title');
  document.querySelector('h1').firstChild.textContent = language === 'en' ? '3D Pinball ' : '3D 핀볼 ';
  document.querySelector('h1 span').textContent = t('subtitle');
  document.querySelector('footer').textContent = t('footer');
  canvas.setAttribute('aria-label', t('canvas'));
  for (const [id, key] of [['retry-save', 'retrySave'], ['apply-update', 'applyUpdate'], ['later-update', 'later'], ['retry-restore', 'retryRestore'], ['continue-unsaved', 'continueUnsaved'], ['retry-load', 'retryLoad']]) byId(id).textContent = t(key);
  setStatus(statusKey);
  setStartup(startupKey);
  if (noticeKey) byId('notice-text').textContent = t(noticeKey);
}

function showNotice(key) {
  noticeKey = key;
  byId('notice-text').textContent = t(key);
  notice.hidden = false;
}
function saveProblem() {
  savingError = true;
  showNotice('saveFailed');
  byId('retry-save').hidden = false;
}
const persistence = createPersistence({
  sync: () => syncFileSystem(globalThis.Module.FS, false),
  onError: saveProblem,
  onSaved: () => {
    savingError = false;
    byId('retry-save').hidden = true;
    if (waitingWorker && !updateDeferred) offerUpdate();
    else if (!updating) notice.hidden = true;
  }
});
globalThis.pinballPersistenceFlush = () => persistence.flush();

function callCore(name, ...args) {
  const fn = globalThis.Module?.[`_${name}`];
  if (ready && typeof fn === 'function') return fn(...args);
}
function cancelInputs() { callCore('platform_cancel_inputs'); }
function suspend() {
  nameInput.suspend();
  cancelInputs();
  callCore('platform_notify_pause', 1, 0);
  callCore('platform_flush_settings');
  if (mounted && restoreReleased) void persistence.flush();
}
function activateAudio() {
  callCore('platform_activate_audio');
  const audioContext = globalThis.Module?.SDL2?.audioContext;
  if (audioContext?.state === 'suspended') void audioContext.resume().catch(() => {});
}
function activate() {
  canvas.focus({ preventScroll: true });
  activateAudio();
}
const nameInput = createNameInput({ document, window, canvas, activateAudio, cancelInputs,
  edit: (session, value, cursor, anchor) => {
    if (ready) globalThis.Module.ccall('platform_web_name_edit', 'number', ['number', 'string', 'number', 'number'], [session, value, cursor, anchor]);
  },
  submit: session => callCore('platform_web_name_submit', session)
});
globalThis.pinballWebNameInput = (...args) => nameInput.update(...args);
canvas.addEventListener('pointerdown', activate);
canvas.addEventListener('keydown', activate);
canvas.addEventListener('contextmenu', event => event.preventDefault());
canvas.addEventListener('pointercancel', cancelInputs);
canvas.addEventListener('lostpointercapture', event => {
  // Normal implicit capture release follows pointerup (buttons=0); SDL has
  // already released that finger. Preserve any other finger still held.
  if (event.buttons) cancelInputs();
});
window.addEventListener('blur', suspend);
window.addEventListener('pagehide', suspend);
document.addEventListener('visibilitychange', () => {
  if (document.hidden) suspend();
  // The shared game's Resume command controls continuation on return.
});

const canvasSizer = createCanvasResizer({
  stageSize: () => ({ width: byId('stage').clientWidth, height: byId('stage').clientHeight }),
  bufferSize: () => ({ width: canvas.width, height: canvas.height }),
  applyCssSize: size => {
    canvas.style.width = `${size.width}px`;
    canvas.style.height = `${size.height}px`;
  },
  notifyResize: () => window.dispatchEvent(new Event('resize')),
  schedule: callback => requestAnimationFrame(callback)
});
new ResizeObserver(() => canvasSizer.update()).observe(byId('stage'));
new MutationObserver(() => canvasSizer.update()).observe(canvas, { attributes: true, attributeFilter: ['width', 'height'] });
function resizeVisualViewport() {
  if (window.visualViewport) byId('app').style.height = `${window.visualViewport.height}px`;
  canvasSizer.update();
}
window.visualViewport?.addEventListener('resize', resizeVisualViewport);
resizeVisualViewport();

function releaseRestore() {
  if (restoreReleased) return;
  restoreReleased = true;
  globalThis.Module.removeRunDependency('pinball-persistent-restore');
}
async function restore() {
  byId('retry-restore').hidden = true;
  byId('continue-unsaved').hidden = true;
  setStartup('restoring');
  try {
    const fs = globalThis.Module.FS;
    if (!mounted) {
      if (!fs.analyzePath('/persistent').exists) fs.mkdir('/persistent');
      fs.mount(fs.filesystems.IDBFS, {}, '/persistent');
      mounted = true;
    }
    await syncFileSystem(fs, true);
    releaseRestore();
  } catch {
    setStartup('restoreFailed');
    byId('retry-restore').hidden = false;
    byId('continue-unsaved').hidden = false;
  }
}
byId('retry-restore').addEventListener('click', () => void restore());
byId('continue-unsaved').addEventListener('click', () => {
  persistence.disable();
  showNotice('unsaved');
  releaseRestore();
});
byId('retry-save').addEventListener('click', async () => {
  callCore('platform_flush_settings');
  await persistence.flush();
});
byId('retry-load').addEventListener('click', () => location.reload());

function coreReady() {
  if (ready) return;
  ready = true;
  startup.hidden = true;
  setStatus(offlineReady ? 'offlineReady' : 'ready');
  refreshLanguage();
  canvasSizer.markReady();
  if (document.hidden) suspend();
}
globalThis.pinballCoreReady = coreReady;
globalThis.pinballStorageError = () => {
  showNotice('damagedSave');
};
function loadFailure(key) {
  ready = false;
  startup.hidden = false;
  setStartup(key);
  byId('retry-load').hidden = false;
  setStatus('loadFailed');
}
globalThis.Module = {
  canvas,
  noExitRuntime: true,
  locateFile: path => new URL(path, import.meta.url).href,
  preRun: [() => {
    globalThis.Module.addRunDependency('pinball-persistent-restore');
    void restore();
  }],
  postRun: [],
  print: () => {},
  printErr: message => console.error('[pinball]', message),
  onAbort: () => loadFailure('runtimeFailed'),
  setStatus: text => { if (!ready && text) setStatus('preparing'); }
};

function offerUpdate() {
  if (!waitingWorker || updateDeferred) return;
  showNotice('updateReady');
  byId('apply-update').hidden = false;
  byId('later-update').hidden = false;
}
byId('later-update').addEventListener('click', () => {
  updateDeferred = true;
  byId('apply-update').hidden = true;
  byId('later-update').hidden = true;
  if (!savingError) notice.hidden = true;
});
byId('apply-update').addEventListener('click', async () => {
  if (!waitingWorker || updating || !ready) return;
  updating = true;
  byId('apply-update').disabled = true;
  suspend();
  const saved = persistence.enabled && await persistence.flush();
  if (!saved) {
    updating = false;
    byId('apply-update').disabled = false;
    showNotice('updateSaveFailed');
    return;
  }
  waitingWorker.postMessage({ type: 'PINBALL_APPLY_UPDATE' });
});

async function registerOffline() {
  if (!('serviceWorker' in navigator)) {
    setStatus('unsupported');
    return;
  }
  let refreshForUpdate = false;
  navigator.serviceWorker.addEventListener('controllerchange', () => {
    if (updating && !refreshForUpdate) {
      refreshForUpdate = true;
      location.reload();
    }
  });
  navigator.serviceWorker.addEventListener('message', event => {
    if (event.data?.type === 'PINBALL_CACHE_READY') {
      offlineReady = true;
      setStatus('offlineReady');
    }
  });
  try {
    const registration = await navigator.serviceWorker.register('./sw.js', { updateViaCache: 'none' });
    registration.active?.postMessage({ type: 'PINBALL_QUERY_CACHE' });
    const checkWaiting = () => {
      if (registration.waiting && navigator.serviceWorker.controller) {
        waitingWorker = registration.waiting;
        offerUpdate();
      }
    };
    checkWaiting();
    registration.addEventListener('updatefound', () => {
      const worker = registration.installing;
      worker?.addEventListener('statechange', () => {
        if (worker.state === 'installed') checkWaiting();
        if (worker.state === 'redundant') showNotice(navigator.serviceWorker.controller ? 'updateCacheFailed' : 'cacheFailed');
      });
    });
  } catch {
    showNotice('cacheFailed');
  }
}

const engineScript = document.createElement('script');
engineScript.src = new URL('./SpaceCadetPinball.js', import.meta.url).href;
engineScript.addEventListener('error', () => loadFailure('scriptFailed'));
document.head.append(engineScript);
void registerOffline();
setInterval(refreshLanguage, 500);
