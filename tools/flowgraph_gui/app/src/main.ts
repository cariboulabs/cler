import { mount } from 'svelte';
import App from './App.svelte';
import { inTauri } from './lib/backend';
import { shim } from './lib/webshim';
import './app.css';

// ponytail: web mode = static bundle + e2e_backend on the same origin; set VITE_CLER_WEB_BACKEND (a URL prefix, '' for same origin) at build time.
const backend = import.meta.env.VITE_CLER_WEB_BACKEND as string | undefined;
if (backend !== undefined && !inTauri()) shim(backend);

const target = document.getElementById('app');
if (!target) throw new Error('missing #app mount point');

export default mount(App, { target });
