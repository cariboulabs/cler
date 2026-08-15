// Three jobs, all needed because GitHub Pages serves plain static files:
//  1. add COOP/COEP to every response, so pthreads work (the coi-serviceworker trick, MIT,
//     Guido Zuidhof and contributors — https://github.com/gzuidhof/coi-serviceworker);
//  2. mirror the emception toolchain under emception/* so a cross-origin worker bundle
//     becomes a same-origin one that can still resolve its own assets by relative URL;
//  3. serve built/* out of Cache Storage, where a browser build drops its files.
const TOOLCHAIN = new URL(self.location.href).searchParams.get('toolchain');
const TYPES = { html: 'text/html', js: 'text/javascript', wasm: 'application/wasm', json: 'application/json' };

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

function isolated(response, type) {
  const headers = new Headers(response.headers);
  headers.set('Cross-Origin-Opener-Policy', 'same-origin');
  headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
  headers.set('Cross-Origin-Resource-Policy', 'cross-origin');
  if (type) headers.set('Content-Type', type);
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
}

async function mirrored(request, name) {
  const cache = await caches.open('cler-toolchain');
  const hit = await cache.match(name);
  if (hit) return isolated(hit);
  const response = await fetch(TOOLCHAIN + name, { mode: 'cors' });
  if (!response.ok) return response;
  await cache.put(name, response.clone());
  for (const client of await self.clients.matchAll()) {
    client.postMessage({ toolchain: name, bytes: Number(response.headers.get('content-length') ?? 0) });
  }
  return isolated(response);
}

self.addEventListener('fetch', (event) => {
  const request = event.request;
  if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') return;
  const url = new URL(request.url);
  const mirror = url.origin === self.location.origin && url.pathname.match(/\/emception\/([^/]+)$/);
  if (mirror && TOOLCHAIN) {
    event.respondWith(mirrored(request, mirror[1]));
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
