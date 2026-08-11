import { defineConfig } from 'playwright/test';

const BACKEND_PORT = Number(process.env.CLER_E2E_PORT ?? 8791);
const FRONTEND = 'http://localhost:1420';

export default defineConfig({
  testDir: '.',
  outputDir: '../../../../build-e2e-gui/test-results',
  fullyParallel: false,
  workers: 1,
  timeout: 90_000,
  expect: { timeout: 15_000 },
  reporter: [['list']],
  use: {
    baseURL: FRONTEND,
    channel: 'chrome',
    viewport: { width: 1600, height: 1000 },
    trace: 'retain-on-failure'
  },
  webServer: [
    {
      command: 'npm run dev',
      cwd: '..',
      url: FRONTEND,
      reuseExistingServer: true,
      timeout: 120_000
    },
    {
      command: `cargo run --quiet --bin e2e_backend -- ${BACKEND_PORT}`,
      cwd: '../src-tauri',
      url: `http://127.0.0.1:${BACKEND_PORT}/health`,
      reuseExistingServer: true,
      timeout: 600_000,
      stdout: 'pipe',
      stderr: 'pipe'
    }
  ]
});
