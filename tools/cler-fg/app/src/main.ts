import { mount } from 'svelte';
import App from './App.svelte';
import { inTauri } from './lib/backend';
import './app.css';

if (import.meta.env.VITE_CLER_WASM && !inTauri()) {
  const { bootBrowser } = await import('./web/boot');
  await bootBrowser();
}

const target = document.getElementById('app');
if (!target) throw new Error('missing #app mount point');

export default mount(App, { target });
