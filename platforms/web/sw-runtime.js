/* Loaded by the generated sw.js wrapper with PINBALL_BUILD_VERSION. */
'use strict';
const scopeUrl = self.registration.scope;
const cachePrefix = `pinball-assets:${scopeUrl}:`;
const cacheName = `${cachePrefix}${PINBALL_BUILD_VERSION}`;
const manifestUrl = new URL('asset-manifest.json', scopeUrl).href;
let manifestPromise;

function validateManifest(manifest) {
  if (manifest?.schema !== 1 || manifest.version !== PINBALL_BUILD_VERSION || !Array.isArray(manifest.assets) || !manifest.assets.length) {
    throw new Error('Invalid asset set');
  }
  const seen = new Set();
  for (const asset of manifest.assets) {
    if (typeof asset.url !== 'string' || !/^[a-zA-Z0-9_.\/-]+$/.test(asset.url) || asset.url.startsWith('/') || asset.url.split('/').some(part => part === '..' || !part) || seen.has(asset.url) || !/^[0-9a-f]{64}$/.test(asset.sha256) || !Number.isSafeInteger(asset.size) || asset.size < 0) {
      throw new Error('Invalid asset entry');
    }
    seen.add(asset.url);
  }
  if (!seen.has('index.html') || !seen.has('SpaceCadetPinball.js') || !seen.has('SpaceCadetPinball.wasm') || !seen.has('SpaceCadetPinball.data')) throw new Error('Incomplete game assets');
  return manifest;
}
function sha256(buffer) {
  return crypto.subtle.digest('SHA-256', buffer).then(hash => [...new Uint8Array(hash)].map(value => value.toString(16).padStart(2, '0')).join(''));
}
async function installAssets() {
  const cache = await caches.open(cacheName);
  if (await cache.match(manifestUrl)) return;
  try {
    const manifestResponse = await fetch(manifestUrl, { cache: 'no-store' });
    if (!manifestResponse.ok) throw new Error('Asset list unavailable');
    const manifest = validateManifest(await manifestResponse.clone().json());
    for (const asset of manifest.assets) {
      const url = new URL(asset.url, scopeUrl).href;
      const response = await fetch(url, { cache: 'no-store' });
      if (!response.ok) throw new Error('Asset unavailable');
      const bytes = await response.clone().arrayBuffer();
      if (bytes.byteLength !== asset.size || await sha256(bytes) !== asset.sha256) throw new Error('Asset mismatch');
      await cache.put(url, response);
    }
    // This completion record is written last. A partial set is never activated.
    await cache.put(manifestUrl, manifestResponse);
  } catch (error) {
    await caches.delete(cacheName);
    throw error;
  }
}
function cachedManifest() {
  if (!manifestPromise) manifestPromise = caches.open(cacheName).then(cache => cache.match(manifestUrl)).then(response => {
    if (!response) throw new Error('Cache not complete');
    return response.json();
  }).then(validateManifest);
  return manifestPromise;
}
self.addEventListener('install', event => {
  event.waitUntil(installAssets());
  // A replacement waits for an explicit choice; the first install activates normally.
});
self.addEventListener('activate', event => {
  event.waitUntil((async () => {
    await cachedManifest();
    await self.clients.claim();
    const clients = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
    clients.forEach(client => client.postMessage({ type: 'PINBALL_CACHE_READY', version: PINBALL_BUILD_VERSION }));
    // Old complete sets remain usable by already open pages. IndexedDB is untouched.
  })());
});
self.addEventListener('message', event => {
  if (event.data?.type === 'PINBALL_QUERY_CACHE' && event.source?.url?.startsWith(scopeUrl)) {
    event.waitUntil(cachedManifest().then(() => event.source.postMessage({ type: 'PINBALL_CACHE_READY', version: PINBALL_BUILD_VERSION })));
  }
  if (event.data?.type === 'PINBALL_APPLY_UPDATE' && event.source?.url?.startsWith(scopeUrl)) {
    event.waitUntil(cachedManifest().then(() => self.skipWaiting()));
  }
});
self.addEventListener('fetch', event => {
  const request = event.request;
  const url = new URL(request.url);
  if (request.method !== 'GET' || !url.href.startsWith(scopeUrl)) return;
  event.respondWith((async () => {
    const manifest = await cachedManifest();
    const cache = await caches.open(cacheName);
    if (request.mode === 'navigate') return await cache.match(new URL('index.html', scopeUrl).href);
    const relative = url.pathname.slice(new URL(scopeUrl).pathname.length);
    if (relative === 'asset-manifest.json' || manifest.assets.some(asset => asset.url === relative)) {
      const result = await cache.match(new URL(relative, scopeUrl).href);
      if (result) return result;
      // A missing item must not silently mix files from another build.
      return new Response('Offline game files are incomplete.', { status: 503, headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
    }
    return fetch(request);
  })());
});
