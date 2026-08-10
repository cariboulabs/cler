import { chromium, type Browser, type Page } from 'playwright';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createServer, type ViteDevServer } from 'vite';
import { fixtures } from '../src/fixtures';
import { projectSite } from '../src/lib/project';
import type { Site } from '../src/lib/schema';

const MIN_VISIBLE_FRACTION = 0.95;
const BOOT_TIMEOUT = 120_000;
const CASE_TIMEOUT = 60_000;

type RenderedEdge = { id: string; length: number; visible: number };

let server: ViteDevServer;
let browser: Browser;
let page: Page;
let origin: string;

function firstSite(name: string): Site {
  const site = fixtures[name]?.sites[0];
  if (!site) throw new Error(`fixture ${name} has no sites`);
  return site;
}

async function measureEdges(fixture: string): Promise<RenderedEdge[]> {
  await page.goto(`${origin}/?fixture=${fixture}`, { waitUntil: 'load' });
  await page.waitForSelector('.svelte-flow__node');
  await page.waitForSelector('path.svelte-flow__edge-path');
  await page.waitForTimeout(500);

  return page.evaluate(() => {
    const pane = document.querySelector('.svelte-flow__pane');
    if (!pane) throw new Error('flow pane never rendered');
    const paneBox = pane.getBoundingClientRect();
    const nodeBoxes = Array.from(document.querySelectorAll('.svelte-flow__node')).map((node) =>
      node.getBoundingClientRect()
    );
    const hidden = (x: number, y: number) =>
      x < paneBox.left ||
      x > paneBox.right ||
      y < paneBox.top ||
      y > paneBox.bottom ||
      nodeBoxes.some((box) => x >= box.left && x <= box.right && y >= box.top && y <= box.bottom);

    const samples = 100;
    return Array.from(document.querySelectorAll('.svelte-flow__edge')).map((group) => {
      const path = group.querySelector('path.svelte-flow__edge-path');
      if (!(path instanceof SVGPathElement)) throw new Error('edge rendered without a path');
      const matrix = path.getScreenCTM();
      if (!matrix) throw new Error('edge path has no screen geometry');
      const length = path.getTotalLength();
      let clear = 0;
      for (let i = 0; i <= samples; i++) {
        const point = path.getPointAtLength((length * i) / samples);
        const x = matrix.a * point.x + matrix.c * point.y + matrix.e;
        const y = matrix.b * point.x + matrix.d * point.y + matrix.f;
        if (!hidden(x, y)) clear++;
      }
      return {
        id: group.getAttribute('data-id') ?? '',
        length,
        visible: clear / (samples + 1)
      };
    });
  });
}

beforeAll(async () => {
  server = await createServer({ logLevel: 'silent', server: { port: 0, strictPort: false } });
  await server.listen();
  const url = server.resolvedUrls?.local[0];
  if (!url) throw new Error('vite dev server reported no url');
  origin = url.replace(/\/$/, '');
  browser = await chromium.launch({ channel: 'chrome' });
  page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
}, BOOT_TIMEOUT);

afterAll(async () => {
  await browser?.close();
  await server?.close();
});

describe('every fixture renders its edges on screen', () => {
  for (const name of Object.keys(fixtures)) {
    it(
      name,
      async () => {
        const projected = projectSite(firstSite(name)).edges;
        const rendered = await measureEdges(name);

        expect(rendered.map((edge) => edge.id).sort()).toEqual(
          projected.map((edge) => edge.id).sort()
        );

        for (const edge of rendered) {
          expect(edge.length).toBeGreaterThan(0);
          expect(edge.visible).toBeGreaterThanOrEqual(MIN_VISIBLE_FRACTION);
        }
      },
      CASE_TIMEOUT
    );
  }
});
