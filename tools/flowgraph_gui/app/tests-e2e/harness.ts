import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { test as base, expect, type Locator, type Page } from 'playwright/test';
import { shim } from '../src/lib/webshim';

export { expect };

export const BACKEND = process.env.CLER_E2E_BACKEND ?? 'http://127.0.0.1:8791';

export function repoRoot(): string {
  let dir = process.cwd();
  while (!existsSync(join(dir, 'desktop_examples'))) {
    const up = dirname(dir);
    if (up === dir) throw new Error(`cler repo root not found above ${process.cwd()}`);
    dir = up;
  }
  return dir;
}

export type Call = { cmd: string; args: Record<string, unknown> };

export type Work = {
  dir: string;
  copy: (example: string) => string;
  write: (name: string, text: string) => string;
  bytes: (path: string) => string;
};

export type Cler = {
  work: Work;
  shot: (name: string) => Promise<void>;
  calls: (cmd?: string) => Promise<Call[]>;
  forget: () => Promise<void>;
  openFile: (path: string) => Promise<string>;
  emit: (event: string, payload: unknown) => Promise<void>;
  node: (blockVar: string) => Locator;
  handle: (blockVar: string, handleId: string) => Locator;
  dragWire: (from: string, to: string, targetHandle: string) => Promise<void>;
};

function workspace(): Work {
  const root = join(repoRoot(), 'build-e2e-gui');
  mkdirSync(root, { recursive: true });
  const dir = mkdtempSync(join(root, 'run-'));
  return {
    dir,
    copy(example) {
      const target = join(dir, example);
      copyFileSync(join(repoRoot(), 'desktop_examples', example), target);
      return target;
    },
    write(name, text) {
      const target = join(dir, name);
      writeFileSync(target, text);
      return target;
    },
    bytes(path) {
      return readFileSync(path, 'utf8');
    }
  };
}

export const test = base.extend<Cler>({
  page: async ({ page }, use) => {
    await page.addInitScript(shim, BACKEND);
    await use(page);
  },

  work: async ({}, use) => {
    const work = workspace();
    await use(work);
    rmSync(work.dir, { recursive: true, force: true });
  },

  shot: async ({ page }, use, info) => {
    const target = process.env.CLER_SHOTS;
    let ordinal = 0;
    await use(async (name: string) => {
      if (!target) return;
      mkdirSync(target, { recursive: true });
      ordinal += 1;
      const slug = `${info.title.replace(/[^a-z0-9]+/gi, '-')}-${ordinal}-${name}`;
      await page.screenshot({ path: join(target, `${slug}.png`) });
    });
  },

  calls: async ({ page }, use) => {
    await use(async (cmd?: string) => {
      const seen = (await page.evaluate(
        () => (window as unknown as { __CLER_E2E__: { calls: Call[] } }).__CLER_E2E__.calls
      )) as Call[];
      return cmd === undefined ? seen : seen.filter((call) => call.cmd === cmd);
    });
  },

  forget: async ({ page }, use) => {
    await use(async () => {
      await page.evaluate(() =>
        (window as unknown as { __CLER_E2E__: { forget: () => void } }).__CLER_E2E__.forget()
      );
    });
  },

  openFile: async ({ page }, use) => {
    await use(async (path: string) => {
      await page.evaluate(
        (answer) =>
          (
            window as unknown as { __CLER_E2E__: { answerDialog: (value: unknown) => void } }
          ).__CLER_E2E__.answerDialog(answer),
        path
      );
      await page.getByTestId('file-menu').click();
      await page.getByTestId('file-open').click();
      const shown = page.getByTestId('doc-path');
      await expect(shown).toHaveAttribute('title', resolve(path), { timeout: 30_000 });
      await expect(page.locator('.svelte-flow__node').first()).toBeVisible({ timeout: 30_000 });
      const inspector = page.getByTestId('rail-tab-inspector');
      if ((await inspector.getAttribute('aria-selected')) !== 'true') await inspector.click();
      return resolve(path);
    });
  },

  emit: async ({ page }, use) => {
    await use(async (event: string, payload: unknown) => {
      await page.evaluate(
        ([name, sent]) =>
          (
            window as unknown as {
              __CLER_E2E__: { emit: (event: string, payload: unknown) => void };
            }
          ).__CLER_E2E__.emit(name as string, sent),
        [event, payload] as [string, unknown]
      );
    });
  },

  node: async ({ page }, use) => {
    await use((blockVar: string) => page.locator(`.svelte-flow__node[data-id="${blockVar}"]`));
  },

  handle: async ({ page }, use) => {
    await use((blockVar: string, handleId: string) =>
      page.locator(
        `.svelte-flow__node[data-id="${blockVar}"] .svelte-flow__handle[data-handleid="${handleId}"]`
      )
    );
  },

  dragWire: async ({ page, handle }, use) => {
    await use(async (from: string, to: string, targetHandle: string) => {
      const source = await centre(handle(from, 'out'));
      const target = await centre(handle(to, targetHandle));
      await page.mouse.move(source.x, source.y);
      await page.mouse.down();
      await page.mouse.move((source.x + target.x) / 2, (source.y + target.y) / 2, { steps: 8 });
      await page.mouse.move(target.x, target.y, { steps: 8 });
      await page.mouse.up();
    });
  }
});

export async function saveFile(page: Page): Promise<void> {
  await page.getByTestId('file-menu').click();
  await page.getByTestId('file-save').click();
}

async function centre(locator: Locator): Promise<{ x: number; y: number }> {
  await expect(locator).toBeVisible();
  const box = await locator.boundingBox();
  if (!box) throw new Error('handle has no box');
  return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
}
