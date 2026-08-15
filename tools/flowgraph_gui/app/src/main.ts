import { mount } from 'svelte';
import App from './App.svelte';
import { inTauri } from './lib/backend';
import './app.css';

// VITE_CLER_WASM: no server at all — the editor session runs in cler_web.wasm over the bundled examples and block headers.
if (import.meta.env.VITE_CLER_WASM && !inTauri()) {
  // pthreads in the run window need cross-origin isolation; GitHub Pages sends no COOP/COEP
  // headers, so a service worker adds them (reloads once on first visit).
  if (!window.crossOriginIsolated) {
    const coi = document.createElement('script');
    coi.src = `${import.meta.env.BASE_URL}coi-serviceworker.min.js`;
    document.head.appendChild(coi);
  }
  const { installWasmShell } = await import('./lib/wasmbridge');
  const { browserFiles, runnableExamples } = await import('./fixtures/files');
  await installWasmShell(browserFiles, runnableExamples);
}

const target = document.getElementById('app');
if (!target) throw new Error('missing #app mount point');

export default mount(App, { target });
