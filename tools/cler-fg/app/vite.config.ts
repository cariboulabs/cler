import { defineConfig, type Plugin } from 'vitest/config';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import path from 'node:path';
import fs from 'node:fs';

// The Run button opens ../../demos/run/<name>.html — on Pages that is
// docs/demos/run/, in dev this middleware serves the same files.
const demosDir = path.resolve(__dirname, '../../../docs/demos/run');
const serveDemos = (): Plugin => ({
  name: 'serve-demos-run',
  configureServer(server) {
    server.middlewares.use('/demos/run', (req, res, next) => {
      const file = path.join(demosDir, decodeURIComponent((req.url ?? '/').split('?')[0]));
      if (!file.startsWith(demosDir) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) return next();
      const type = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' }[path.extname(file)];
      if (type) res.setHeader('Content-Type', type);
      fs.createReadStream(file).pipe(res);
    });
  }
});

export default defineConfig({
  plugins: [svelte(), serveDemos()],
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    watch: { ignored: ['**/target/**', '**/build/**'] },
    fs: { allow: ['.', '../cler-graph/tests/data', '../../../desktop_examples', '../../../desktop_blocks'] }
  },
  build: { target: 'es2022' },
  test: {
    include: ['tests/**/*.test.ts'],
    environment: 'node',
    fileParallelism: false,
    testTimeout: 90_000,
    hookTimeout: 180_000
  }
});
