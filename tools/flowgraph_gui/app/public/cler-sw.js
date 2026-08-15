// Three jobs, all needed because GitHub Pages serves plain static files:
//  1. add COOP/COEP to every response, so pthreads work (the coi-serviceworker trick, MIT,
//     Guido Zuidhof and contributors — https://github.com/gzuidhof/coi-serviceworker);
//  2. mirror the emception toolchain under emception/* so a cross-origin worker bundle
//     becomes a same-origin one that can still resolve its own assets by relative URL —
//     verifying each pinned file's sha256 before it is cached, because that bundle then
//     runs as same-origin code;
//  3. serve built/* out of Cache Storage, where a browser build drops its files.
const params = new URL(self.location.href).searchParams;
const TOOLCHAIN = params.get('toolchain');
const PINS = JSON.parse(params.get('pins') || '{}');
const CACHE = `cler-toolchain:${TOOLCHAIN}`;
const TYPES = { html: 'text/html', js: 'text/javascript', wasm: 'application/wasm', json: 'application/json' };

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) =>
  event.waitUntil(
    (async () => {
      await self.clients.claim();
      // A different TOOLCHAIN means different content-hashed asset names: a stale cache would
      // answer the bundle but 404 everything it asks for.
      for (const name of await caches.keys()) {
        if (name.startsWith('cler-toolchain:') && name !== CACHE) await caches.delete(name);
      }
    })()
  )
);

function isolated(response, type) {
  const headers = new Headers(response.headers);
  headers.set('Cross-Origin-Opener-Policy', 'same-origin');
  headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
  headers.set('Cross-Origin-Resource-Policy', 'cross-origin');
  if (type) headers.set('Content-Type', type);
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
}

async function post(message) {
  for (const client of await self.clients.matchAll()) client.postMessage(message);
}

function fail(message) {
  void post({ toolchainError: message });
  return new Response(message, { status: 502, headers: { 'Content-Type': 'text/plain' } });
}

async function mirrored(name) {
  const cache = await caches.open(CACHE);
  const hit = await cache.match(name);
  if (hit) return isolated(hit);
  let response;
  try {
    response = await fetch(TOOLCHAIN + name, { mode: 'cors' });
  } catch (error) {
    return fail(`cannot reach the C++ toolchain at ${TOOLCHAIN} — ${error}`);
  }
  if (!response.ok) return fail(`the C++ toolchain host answered ${response.status} for ${name}`);
  const body = await response.arrayBuffer();
  // Only the four entry files are pinned by hash. Everything else the bundle pulls is named by
  // its own content hash, and the bundle asking for it is itself pinned — so the *set* of names
  // is fixed. Pinning their bytes too needs a generated table; add one if the host is ever
  // untrusted rather than merely unversioned.
  const pinned = PINS[name];
  if (pinned) {
    const digest = await crypto.subtle.digest('SHA-256', body);
    const seen = Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, '0')).join('');
    if (seen !== pinned) return fail(`${name} does not match its pinned sha256 — refusing to run it`);
  }
  const verified = new Response(body, { headers: response.headers });
  await cache.put(name, verified.clone());
  void post({ toolchain: name, bytes: body.byteLength });
  return isolated(verified);
}

self.addEventListener('fetch', (event) => {
  const request = event.request;
  if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') return;
  const url = new URL(request.url);
  const mirror = url.origin === self.location.origin && url.pathname.match(/\/emception\/([^/]+)$/);
  if (mirror && TOOLCHAIN) {
    event.respondWith(mirrored(mirror[1]));
    return;
  }
  if (url.origin === self.location.origin && url.pathname.includes('/built/')) {
    event.respondWith(
      caches.open('cler-built').then(async (cache) => {
        const hit = await cache.match(url.pathname);
        return hit ? isolated(hit, TYPES[url.pathname.split('.').pop()]) : new Response('not built', { status: 404 });
      })
    );
    return;
  }
  event.respondWith(fetch(request).then((response) => (response.status === 0 ? response : isolated(response))));
});
