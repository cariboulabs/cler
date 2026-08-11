import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { test as base, expect, type Locator, type Page } from 'playwright/test';

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

function shim(base: string): void {
  const win = window as unknown as Record<string, unknown>;
  const calls: { cmd: string; args: Record<string, unknown> }[] = [];
  const listeners = new Map<number, { event: string; handler: number }>();
  const callbacks = new Map<number, (payload: unknown) => void>();
  let next = 1;
  let dialogAnswer: unknown = null;

  const post = async (route: string, payload: unknown): Promise<unknown> => {
    const response = await fetch(base + route, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    const text = await response.text();
    const body = text.length > 0 ? JSON.parse(text) : {};
    if (response.status !== 200) {
      throw new Error(`e2e backend ${response.status}: ${body.loud ?? text}`);
    }
    if ('err' in body) throw body.err;
    return body.ok;
  };

  const invoke = async (cmd: string, args: Record<string, unknown> = {}): Promise<unknown> => {
    calls.push({ cmd, args });
    if (cmd === 'plugin:dialog|open') return dialogAnswer;
    if (cmd === 'plugin:event|listen') {
      const id = next++;
      listeners.set(id, { event: args.event as string, handler: args.handler as number });
      return id;
    }
    if (cmd === 'plugin:event|unlisten') {
      listeners.delete(args.eventId as number);
      return null;
    }
    return post('/invoke', { cmd, args });
  };

  win.__TAURI_INTERNALS__ = {
    invoke,
    transformCallback(callback: (payload: unknown) => void) {
      const id = next++;
      callbacks.set(id, callback);
      return id;
    },
    metadata: {}
  };
  win.__TAURI_EVENT_PLUGIN_INTERNALS__ = {
    unregisterListener(_event: string, eventId: number) {
      listeners.delete(eventId);
    }
  };
  win.__CLER_E2E__ = {
    calls,
    forget: () => calls.splice(0, calls.length),
    answerDialog: (value: unknown) => {
      dialogAnswer = value;
    },
    emit: (event: string, payload: unknown) => {
      for (const [id, entry] of listeners) {
        if (entry.event !== event) continue;
        callbacks.get(entry.handler)?.({ event, id, payload });
      }
    }
  };

  let ticking = false;
  setInterval(() => {
    if (ticking || listeners.size === 0) return;
    ticking = true;
    void post('/events/poll', {})
      .then((pending) => {
        for (const sent of (pending as { event: string; payload: unknown }[]) ?? []) {
          for (const [id, entry] of listeners) {
            if (entry.event !== sent.event) continue;
            const callback = callbacks.get(entry.handler);
            if (callback) callback({ event: sent.event, id, payload: sent.payload });
          }
        }
      })
      .catch(() => undefined)
      .finally(() => {
        ticking = false;
      });
  }, 200);
}

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
      await page.locator('aside.sidebar button.primary').click();
      const shown = page.locator('aside.sidebar .path');
      await expect(shown).toHaveAttribute('title', resolve(path), { timeout: 30_000 });
      await expect(page.locator('.svelte-flow__node').first()).toBeVisible({ timeout: 30_000 });
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

async function centre(locator: Locator): Promise<{ x: number; y: number }> {
  await expect(locator).toBeVisible();
  const box = await locator.boundingBox();
  if (!box) throw new Error('handle has no box');
  return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
}

export async function revision(page: Page): Promise<number> {
  const text = await page.locator('aside.sidebar dl dd').nth(1).textContent();
  return Number(text);
}
