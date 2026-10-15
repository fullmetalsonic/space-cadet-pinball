import test from 'node:test';
import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import vm from 'node:vm';
import { createPersistence, syncFileSystem } from '../persistence.mjs';
import { createCanvasResizer } from '../layout.mjs';
import { strings } from '../locale.mjs';
import { createNameInput } from '../name-input.mjs';

class Target {
  listeners = new Map();
  hidden = false;
  textContent = '';
  style = {};
  firstChild = { textContent: '' };
  width = 800;
  height = 680;
  clientWidth = 390;
  clientHeight = 650;
  addEventListener(name, callback) {
    if (!this.listeners.has(name)) this.listeners.set(name, []);
    this.listeners.get(name).push(callback);
  }
  async fire(name, event = {}) { for (const callback of this.listeners.get(name) ?? []) await callback(event); }
  dispatchEvent(event) { for (const callback of this.listeners.get(event.type) ?? []) callback(event); return true; }
  setAttribute(name, value) { this[name] = value; }
  focus() { this.focused = true; }
  blur() { this.focused = false; }
  setSelectionRange(start, end) { this.selectionStart = start; this.selectionEnd = end; }
}
async function harness({ restoreError = false, updateWaiting = false } = {}) {
  const targets = new Map();
  const target = id => { if (!targets.has(id)) targets.set(id, new Target()); return targets.get(id); };
  const document = new Target();
  document.hidden = false;
  document.documentElement = {};
  document.getElementById = target;
  document.querySelector = target;
  document.head = { append: script => { document.script = script; } };
  document.body = { append: input => { document.nameInput = input; } };
  document.createElement = () => new Target();
  const window = new Target();
  const serviceWorker = new Target();
  const commands = [];
  const dependencies = new Set();
  const writes = [];
  const mounted = [];
  let saved = { language: 'en', highScore: 45678 };
  const state = { restoreError, saved, syncCalls: [], commands, dependencies, writes, mounted, frames: [], reloads: 0, initialized: false, language: 0 };
  const worker = { postMessage: message => commands.push(['worker', message.type]) };
  const registration = new Target();
  registration.waiting = updateWaiting ? worker : null;
  serviceWorker.controller = updateWaiting ? {} : null;
  serviceWorker.register = async () => registration;
  const source = (await fs.readFile(new URL('../bootstrap.mjs', import.meta.url), 'utf8'))
    .replace(/^import .*;\r?\n/gm, '')
    .replaceAll('import.meta.url', '"https://pinball.test/game/bootstrap.mjs"');
  const context = vm.createContext({
    createPersistence, syncFileSystem, createCanvasResizer, strings, createNameInput,
    document, window, navigator: { serviceWorker },
    ResizeObserver: class { constructor(callback) { this.callback = callback; } observe() { state.resizeObserved = this.callback; this.callback(); } },
    MutationObserver: class { constructor(callback) { state.attributesObserved = callback; } observe() {} },
    location: { reload: () => { state.reloads++; } },
    requestAnimationFrame: callback => { state.frames.push(callback); },
    setInterval: callback => { state.refreshLanguage = callback; }, URL, Event, console
  });
  vm.runInContext(source, context);
  const Module = context.Module;
  Module.FS = {
    filesystems: { IDBFS: {} },
    analyzePath: () => ({ exists: false }),
    mkdir: path => mounted.push(['mkdir', path]),
    mount: (kind, options, path) => mounted.push(['mount', path]),
    syncfs: (populate, callback) => {
      state.syncCalls.push(populate);
      queueMicrotask(() => {
        if (populate && state.restoreError) callback(new Error('database unavailable'));
        else {
          if (populate) state.restored = { ...saved };
          else { saved = { ...state.restored }; state.saved = saved; writes.push({ ...saved }); }
          callback(null);
        }
      });
    }
  };
  Module.addRunDependency = name => dependencies.add(name);
  Module.removeRunDependency = name => { dependencies.delete(name); state.initialized = true; };
  Module._platform_cancel_inputs = () => commands.push(['cancel']);
  Module._platform_notify_pause = (paused, session) => commands.push(['pause', paused, session]);
  Module._platform_flush_settings = () => { commands.push(['flush']); state.restored = { language: 'ko', highScore: 78901 }; };
  Module._platform_activate_audio = () => commands.push(['audio']);
  Module._platform_is_english = () => state.language;
  await Promise.resolve();
  return { state, target, document, window, serviceWorker, context, Module };
}
const turn = () => new Promise(resolve => setImmediate(resolve));

test('bootstrap requests the existing browser resize after readiness and keeps backing updates with SDL', async () => {
  const h = await harness();
  const canvas = h.target('canvas');
  const stage = h.target('stage');
  stage.clientWidth = 359;
  stage.clientHeight = 704;
  canvas.width = 600;
  canvas.height = 512;
  h.state.resizeObserved();
  assert.equal(canvas.style.width, '359px');
  assert.equal(canvas.style.height, '704px');
  assert.equal(canvas.width, 600);
  assert.equal(h.state.frames.length, 0);
  let resized = 0;
  h.window.addEventListener('resize', () => {
    resized++;
    // The SDK callback owns backing size; this test supplies its response.
    canvas.width = parseInt(canvas.style.width);
    canvas.height = parseInt(canvas.style.height);
  });
  h.context.pinballCoreReady();
  assert.equal(h.state.frames.length, 1);
  h.state.frames.shift()();
  assert.equal(resized, 1);
  assert.equal(canvas.width, 359);
  assert.equal(canvas.height, 704);
  h.state.attributesObserved();
  assert.equal(h.state.frames.length, 0);
  await canvas.fire('pointercancel');
  assert.equal(h.state.commands.filter(command => command[0] === 'cancel').length, 1);
});

test('bootstrap holds core initialization until IDBFS restore and cancellation never auto-resumes', async () => {
  const h = await harness();
  h.Module.preRun[0]();
  assert.equal(h.state.dependencies.size, 1);
  assert.equal(h.state.initialized, false);
  await turn();
  assert.equal(h.state.dependencies.size, 0);
  assert.deepEqual(h.state.restored, { language: 'en', highScore: 45678 });
  assert.deepEqual(h.state.mounted, [['mkdir', '/persistent'], ['mount', '/persistent']]);
  h.context.pinballCoreReady();
  await h.target('canvas').fire('pointerdown');
  assert.equal(h.target('canvas').focused, true);
  assert.ok(h.state.commands.some(command => command[0] === 'audio'));
  h.document.hidden = true;
  await h.document.fire('visibilitychange');
  await turn();
  assert.deepEqual(h.state.saved, { language: 'ko', highScore: 78901 });
  h.document.hidden = false;
  await h.document.fire('visibilitychange');
  await h.window.fire('focus');
  assert.equal(h.state.commands.filter(command => command[0] === 'pause' && command[1] === 0).length, 0);
  assert.equal(h.state.commands.filter(command => command[0] === 'cancel').length, 1);
});

test('normal pointer capture release preserves other fingers while cancellation and active capture loss release inputs', async () => {
  const h = await harness();
  h.context.pinballCoreReady();
  const canvas = h.target('canvas');
  await canvas.fire('lostpointercapture', { pointerId: 2, pointerType: 'touch', buttons: 0 });
  assert.equal(h.state.commands.filter(command => command[0] === 'cancel').length, 0);
  await canvas.fire('lostpointercapture', { pointerId: 3, pointerType: 'touch', buttons: 1 });
  assert.equal(h.state.commands.filter(command => command[0] === 'cancel').length, 1);
  await canvas.fire('pointercancel', { pointerId: 4, pointerType: 'touch', buttons: 0 });
  assert.equal(h.state.commands.filter(command => command[0] === 'cancel').length, 2);
  await h.window.fire('blur');
  assert.equal(h.state.commands.filter(command => command[0] === 'cancel').length, 3);
  assert.ok(h.state.commands.some(command => command[0] === 'pause' && command[1] === 1));
});

test('failed restore offers retry and unsaved continuation protects existing browser records', async () => {
  const h = await harness({ restoreError: true });
  h.Module.preRun[0]();
  await turn();
  assert.equal(h.state.dependencies.size, 1);
  assert.equal(h.target('retry-restore').hidden, false);
  assert.equal(h.target('continue-unsaved').hidden, false);
  await h.target('continue-unsaved').fire('click');
  assert.equal(h.state.dependencies.size, 0);
  h.context.pinballCoreReady();
  await h.window.fire('pagehide');
  await turn();
  assert.deepEqual(h.state.saved, { language: 'en', highScore: 45678 });
  assert.equal(h.state.writes.length, 0);
  assert.match(h.target('notice-text').textContent, /저장하지 않습니다/);
});

test('language follows core and update waits for successful save before worker activation', async () => {
  const h = await harness({ updateWaiting: true });
  h.Module.preRun[0]();
  await turn();
  h.context.pinballCoreReady();
  h.state.language = 1;
  h.state.refreshLanguage();
  assert.equal(h.document.documentElement.lang, 'en');
  assert.equal(h.target('apply-update').textContent, 'Save and apply update');
  assert.equal(h.state.commands.filter(command => command[0] === 'worker').length, 0);
  await h.target('apply-update').fire('click');
  assert.ok(h.state.writes.length > 0);
  assert.deepEqual(h.state.saved, { language: 'ko', highScore: 78901 });
  assert.equal(h.state.commands.at(-1)[0], 'worker');
  assert.equal(h.state.commands.at(-1)[1], 'PINBALL_APPLY_UPDATE');
  await h.serviceWorker.fire('controllerchange');
  assert.equal(h.state.reloads, 1);
});
