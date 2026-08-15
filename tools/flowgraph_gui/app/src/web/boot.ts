import { blockToolchain, TOOLCHAIN_BASE, TOOLCHAIN_PINS } from './emception';
import { installWasmShell } from './wasmbridge';
import { browserFiles, runnableExamples } from './files';

export async function bootBrowser(): Promise<void> {
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
  await installWasmShell(browserFiles, runnableExamples);
}
