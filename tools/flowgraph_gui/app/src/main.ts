import { mount } from 'svelte';
import App from './App.svelte';
import { inTauri } from './lib/backend';
import './app.css';

// VITE_CLER_WASM: no server at all — the editor session runs in cler_web.wasm over the bundled examples and block headers.
if (import.meta.env.VITE_CLER_WASM && !inTauri()) {
  // GitHub Pages sends no COOP/COEP headers, serves nothing under built/, and cannot proxy
  // the emception toolchain — public/cler-sw.js does all three. The first visit reloads once
  // so this document itself comes back through it and the page is cross-origin isolated.
  const { TOOLCHAIN_BASE } = await import('./lib/emception');
  const base = import.meta.env.BASE_URL;
  await navigator.serviceWorker.register(
    `${base}cler-sw.js?toolchain=${encodeURIComponent(TOOLCHAIN_BASE)}`,
    { scope: base }
  );
  await navigator.serviceWorker.ready;
  if (window.crossOriginIsolated) sessionStorage.removeItem('clerSwReload');
  else if (!sessionStorage.getItem('clerSwReload')) {
    sessionStorage.setItem('clerSwReload', '1');
    location.reload();
  }
  const { installWasmShell } = await import('./lib/wasmbridge');
  const { browserFiles, runnableExamples } = await import('./fixtures/files');
  await installWasmShell(browserFiles, runnableExamples);
}

const target = document.getElementById('app');
if (!target) throw new Error('missing #app mount point');

export default mount(App, { target });
