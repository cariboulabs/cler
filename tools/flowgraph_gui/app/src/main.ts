import { mount } from 'svelte';
import App from './App.svelte';
import { inTauri } from './lib/backend';
import './app.css';

async function bootBrowser() {
  const { TOOLCHAIN_BASE, TOOLCHAIN_PINS, blockToolchain } = await import('./lib/emception');
  const base = import.meta.env.BASE_URL;
  try {
    const query = new URLSearchParams({ toolchain: TOOLCHAIN_BASE, pins: JSON.stringify(TOOLCHAIN_PINS) });
    await navigator.serviceWorker.register(`${base}cler-sw.js?${query}`, { scope: base });
    await navigator.serviceWorker.ready;
    if (window.crossOriginIsolated) sessionStorage.removeItem('clerSwReload');
    else if (!sessionStorage.getItem('clerSwReload')) {
      sessionStorage.setItem('clerSwReload', '1');
      location.reload();
      return;
    }
  } catch (error) {
    console.warn('cler service worker unavailable', error);
    blockToolchain('Build and Run need a service worker; this browser mode disables it');
  }

  const { installWasmShell } = await import('./lib/wasmbridge');
  const { browserFiles, runnableExamples } = await import('./fixtures/files');
  await installWasmShell(browserFiles, runnableExamples);
}

if (import.meta.env.VITE_CLER_WASM && !inTauri()) await bootBrowser();

const target = document.getElementById('app');
if (!target) throw new Error('missing #app mount point');

export default mount(App, { target });
