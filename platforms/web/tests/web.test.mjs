import test from 'node:test';
import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import { webcrypto } from 'node:crypto';
import { createPersistence, syncFileSystem } from '../persistence.mjs';
import { canvasCssSize, createCanvasResizer } from '../layout.mjs';
import { packageWeb } from '../package.mjs';

test('save changes during pending sync are serialized and reach the final write', async () => {
  const pending = [];
  let active = 0;
  let maxActive = 0;
  let calls = 0;
  const saved = [];
  let value = 1;
  const persistence = createPersistence({ sync: () => {
    calls++;
    active++;
    maxActive = Math.max(maxActive, active);
    const snapshot = value;
    return new Promise(resolve => pending.push(() => { active--; saved.push(snapshot); resolve(); }));
  } });
  const first = persistence.flush();
  value = 2;
  const second = persistence.flush();
  value = 3;
  persistence.flush();
  assert.equal(first, second);
  pending.shift()();
  await new Promise(resolve => setImmediate(resolve));
  pending.shift()();
  assert.equal(await first, true);
  assert.equal(calls, 2);
  assert.equal(maxActive, 1);
  assert.deepEqual(saved, [1, 3]);
});

test('save failure stops retries until requested and retains dirty state', async () => {
  let failing = true;
  let calls = 0;
  let errors = 0;
  const persistence = createPersistence({
    sync: async () => { calls++; if (failing) throw new Error('quota'); },
    onError: () => { errors++; }
  });
  assert.equal(await persistence.flush(), false);
  assert.equal(calls, 1);
  assert.equal(errors, 1);
  assert.equal(persistence.error.message, 'quota');
  failing = false;
  assert.equal(await persistence.flush(), true);
  assert.equal(persistence.error, null);
  persistence.disable();
  assert.equal(await persistence.flush(), false);
  assert.equal(calls, 2);
});

test('FS synchronization reports the original error and populate flag', async () => {
  const seen = [];
  const fs = { syncfs(populate, callback) { seen.push(populate); callback(populate ? new Error('unavailable') : null); } };
  await assert.rejects(syncFileSystem(fs, true), /unavailable/);
  await syncFileSystem(fs, false);
  assert.deepEqual(seen, [true, false]);
});

test('representative stage sizes retain CSS logical pixels instead of shrinking the whole UI', () => {
  for (const [width, height] of [[320, 600], [390, 730], [820, 320], [1280, 690]]) {
    assert.deepEqual(canvasCssSize(width, height), { width, height });
  }
  assert.deepEqual(canvasCssSize(359.8, 704.5), { width: 359, height: 704 });
  assert.equal(canvasCssSize(0, 100), null);
  assert.equal(canvasCssSize(100, NaN), null);
});

test('resize waits for core readiness, coalesces observations, and terminates after SDL synchronization', () => {
  const frames = [], css = [], notified = [];
  let stage = { width: 359, height: 704 };
  const buffer = { width: 600, height: 512 };
  const resizer = createCanvasResizer({
    stageSize: () => stage,
    bufferSize: () => buffer,
    applyCssSize: size => css.push(size),
    schedule: callback => frames.push(callback),
    notifyResize: () => {
      notified.push(css.at(-1));
      Object.assign(buffer, css.at(-1)); // Emulate the SDL CSS resize response.
    }
  });
  resizer.update();
  assert.deepEqual(css, [{ width: 359, height: 704 }]);
  assert.deepEqual(buffer, { width: 600, height: 512 });
  assert.equal(frames.length, 0);
  resizer.markReady();
  resizer.update();
  assert.equal(frames.length, 1);
  stage = { width: 796, height: 270 };
  resizer.update();
  assert.equal(frames.length, 1);
  frames.shift()();
  assert.deepEqual(notified, [{ width: 796, height: 270 }]);
  assert.deepEqual(buffer, { width: 796, height: 270 });
  resizer.update(); // MutationObserver after SDL changes the attributes.
  assert.equal(frames.length, 0);
  buffer.width = 600;
  buffer.height = 512; // Core resolution restart restores native dimensions.
  resizer.update();
  assert.equal(frames.length, 1);
  frames.shift()();
  assert.deepEqual(buffer, { width: 796, height: 270 });
  assert.equal(notified.length, 2);
});

test('a collapsed stage before the scheduled frame never resizes SDL to zero', () => {
  const frames = [];
  let stage = { width: 359, height: 704 };
  let notices = 0;
  const resizer = createCanvasResizer({
    stageSize: () => stage, bufferSize: () => ({ width: 600, height: 512 }),
    applyCssSize: () => {}, notifyResize: () => { notices++; }, schedule: callback => frames.push(callback)
  });
  resizer.markReady();
  stage = { width: 0, height: 0 };
  frames.shift()();
  assert.equal(notices, 0);
  stage = { width: 359, height: 704 };
  resizer.update();
  assert.equal(frames.length, 1);
});

async function fixture(t) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'pinball-web-test-'));
  t.after(() => fs.rm(directory, { recursive: true, force: true }));
  const build = path.join(directory, 'build');
  await fs.mkdir(build);
  for (const name of ['SpaceCadetPinball.js', 'SpaceCadetPinball.wasm', 'SpaceCadetPinball.data']) await fs.writeFile(path.join(build, name), `fixture:${name}`);
  const output = path.join(directory, 'v1');
  const manifest = await packageWeb({ buildDirectory: build, outputDirectory: output, sourceVersion: 'fixture-engine' });
  return { directory, build, output, manifest };
}

test('immutable package contains required hash set and refuses incompatible reuse', async t => {
  const { directory, build, output, manifest } = await fixture(t);
  assert.equal(manifest.schema, 1);
  assert.equal(manifest.assets.length, 15);
  const icon = await fs.readFile(path.join(output, 'icon-192.png'));
  assert.equal(icon.readUInt32BE(16), 192);
  assert.equal(icon.readUInt32BE(20), 192);
  assert.ok(manifest.assets.every(asset => /^[a-f0-9]{64}$/.test(asset.sha256)));
  assert.equal((await packageWeb({ buildDirectory: build, outputDirectory: output, sourceVersion: 'fixture-engine' })).version, manifest.version);
  await fs.writeFile(path.join(build, 'SpaceCadetPinball.wasm'), 'new build');
  await assert.rejects(packageWeb({ buildDirectory: build, outputDirectory: output, sourceVersion: 'fixture-engine' }), /another version/);
  const second = await packageWeb({ buildDirectory: build, outputDirectory: path.join(directory, 'v2'), sourceVersion: 'fixture-engine' });
  assert.notEqual(second.version, manifest.version);
  await assert.rejects(packageWeb({ buildDirectory: build, outputDirectory: build, sourceVersion: 'fixture-engine' }), /separate/);
});

class CacheStorage {
  entries = new Map();
  async open(name) {
    if (!this.entries.has(name)) this.entries.set(name, new Map());
    const store = this.entries.get(name);
    const key = request => typeof request === 'string' ? request : request.url;
    return {
      match: async request => store.get(key(request))?.clone(),
      put: async (request, response) => { store.set(key(request), response.clone()); }
    };
  }
  async delete(name) { return this.entries.delete(name); }
}

async function worker(output, storage, { corrupt = null, offline = false } = {}) {
  const manifest = JSON.parse(await fs.readFile(path.join(output, 'asset-manifest.json'), 'utf8'));
  const scope = 'https://pinball.test/game/';
  const listeners = new Map();
  const state = { skipWaiting: 0, claimed: 0, messages: [], requests: [], offline };
  const self = {
    registration: { scope },
    addEventListener: (name, fn) => listeners.set(name, fn),
    skipWaiting: async () => { state.skipWaiting++; },
    clients: {
      claim: async () => { state.claimed++; },
      matchAll: async () => [{ postMessage: message => state.messages.push(message) }]
    }
  };
  const fetch = async request => {
    const url = typeof request === 'string' ? request : request.url;
    state.requests.push(url);
    if (state.offline) throw new Error('offline');
    const relative = new URL(url).pathname.slice('/game/'.length);
    try {
      let bytes = await fs.readFile(path.join(output, relative));
      if (relative === corrupt) bytes = Buffer.from('damaged file');
      return new Response(bytes, { status: 200 });
    } catch { return new Response('missing', { status: 404 }); }
  };
  const context = vm.createContext({ self, caches: storage, fetch, crypto: webcrypto, URL, Response, Uint8Array, PINBALL_BUILD_VERSION: manifest.version });
  vm.runInContext(await fs.readFile(new URL('../sw-runtime.js', import.meta.url), 'utf8'), context);
  async function dispatch(name, extra = {}) {
    let result;
    const event = { ...extra, waitUntil: promise => { result = promise; }, respondWith: promise => { result = promise; } };
    listeners.get(name)(event);
    return await result;
  }
  return { state, dispatch, scope, manifest };
}

test('worker verifies complete package before activation and serves navigation/WASM offline', async t => {
  const { output } = await fixture(t);
  const storage = new CacheStorage();
  const sw = await worker(output, storage);
  await sw.dispatch('install');
  assert.equal(sw.state.skipWaiting, 0);
  await sw.dispatch('activate');
  assert.equal(sw.state.claimed, 1);
  assert.equal(sw.state.messages[0].version, sw.manifest.version);
  const requestCount = sw.state.requests.length;
  sw.state.offline = true;
  const html = await sw.dispatch('fetch', { request: { method: 'GET', url: sw.scope, mode: 'navigate' } });
  assert.match(await html.text(), /id="canvas"/);
  const wasm = await sw.dispatch('fetch', { request: { method: 'GET', url: `${sw.scope}SpaceCadetPinball.wasm`, mode: 'cors' } });
  assert.equal(await wasm.text(), 'fixture:SpaceCadetPinball.wasm');
  assert.equal(sw.state.requests.length, requestCount);
  const replies = [];
  await sw.dispatch('message', { data: { type: 'PINBALL_QUERY_CACHE' }, source: { url: sw.scope, postMessage: reply => replies.push(reply) } });
  assert.equal(replies[0].version, sw.manifest.version);
  await sw.dispatch('message', { data: { type: 'PINBALL_APPLY_UPDATE' }, source: { url: sw.scope } });
  assert.equal(sw.state.skipWaiting, 1);
});

test('corrupt update rejects the full new cache while the previous offline version remains usable', async t => {
  const { directory, build, output } = await fixture(t);
  const storage = new CacheStorage();
  const old = await worker(output, storage);
  await old.dispatch('install');
  await old.dispatch('activate');
  await fs.writeFile(path.join(build, 'SpaceCadetPinball.wasm'), 'new-WASM');
  const nextOutput = path.join(directory, 'v2');
  await packageWeb({ buildDirectory: build, outputDirectory: nextOutput, sourceVersion: 'fixture-engine' });
  const next = await worker(nextOutput, storage, { corrupt: 'SpaceCadetPinball.data' });
  await assert.rejects(next.dispatch('install'), /Asset mismatch/);
  assert.equal(storage.entries.size, 1);
  assert.equal(next.state.skipWaiting, 0);
  old.state.offline = true;
  const wasm = await old.dispatch('fetch', { request: { method: 'GET', url: `${old.scope}SpaceCadetPinball.wasm`, mode: 'cors' } });
  assert.equal(await wasm.text(), 'fixture:SpaceCadetPinball.wasm');
});
