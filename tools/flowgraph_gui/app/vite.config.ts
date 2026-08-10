import { defineConfig } from 'vitest/config';
import { svelte } from '@sveltejs/vite-plugin-svelte';

export default defineConfig({
  plugins: [svelte()],
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    watch: { ignored: ['**/target/**', '**/build/**'] },
    fs: { allow: ['.', '../cler-graph/tests/data', '../../../desktop_examples'] }
  },
  build: { target: 'es2022' },
  test: { include: ['tests/**/*.test.ts'], environment: 'node' }
});
