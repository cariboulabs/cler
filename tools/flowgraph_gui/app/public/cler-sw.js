// COOP/COEP headers adapted from coi-serviceworker (MIT, Guido Zuidhof and contributors).
const params = new URL(self.location.href).searchParams;
const TOOLCHAIN = params.get('toolchain');
const PINS = JSON.parse(params.get('pins') || '{}');
const CACHE = `cler-toolchain:${TOOLCHAIN}:${Object.values(PINS).join('-')}`;
const TYPES = { html: 'text/html', js: 'text/javascript', wasm: 'application/wasm', json: 'application/json' };

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) =>
  event.waitUntil(
    (async () => {
      await self.clients.claim();
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

async function mirrored(name, local) {
  const cache = await caches.open(CACHE);
  const hit = await cache.match(name);
  if (hit) return isolated(hit);
  let response = await fetch(local, { cache: 'no-store' }).catch(() => null);
  if (!response?.ok) {
    try {
      response = await fetch(TOOLCHAIN + name, { mode: 'cors' });
    } catch (error) {
      return fail(`cannot reach the C++ toolchain at ${TOOLCHAIN} — ${error}`);
    }
    if (!response.ok) return fail(`the C++ toolchain host answered ${response.status} for ${name}`);
  }
  const body = await response.arrayBuffer();
  // ponytail: only the entry files are pinned by hash; the rest are named by their own content
  // hash and requested by the pinned bundle, so the set of names is fixed but not the bytes.
  // Generate a full table if the host ever becomes untrusted rather than merely unversioned.
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
    event.respondWith(mirrored(mirror[1], url.href));
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
