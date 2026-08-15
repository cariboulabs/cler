import { mount } from 'svelte';
import App from './App.svelte';
import { inTauri } from './lib/backend';
import './app.css';

// VITE_CLER_WASM: no server at all — the editor session runs in cler_web.wasm over the bundled examples and block headers.
if (import.meta.env.VITE_CLER_WASM && !inTauri()) {
  const { installWasmShell } = await import('./lib/wasmbridge');
  const { browserFiles } = await import('./fixtures/files');
  await installWasmShell(browserFiles);
}

const target = document.getElementById('app');
if (!target) throw new Error('missing #app mount point');

export default mount(App, { target });
