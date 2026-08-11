import { describe, expect, it } from 'vitest';
import type { Page } from 'playwright';
import { asks, boot, CASE, calls, emit, FAKE_PATH, NO_KEY, shot, useBrowser } from './ui';

useBrowser();

const CARET = '[data-testid="assistant-caret"]';
const CHIP = '[data-testid="assistant-chip"]';
const INPUT = '[data-testid="assistant-input"]';
const MESSAGE = '[data-testid="assistant-message"]';
const SEND = '[data-testid="assistant-send"]';
const STOP = '[data-testid="assistant-stop"]';
const USAGE = '[data-testid="assistant-usage"]';

async function openAssistant(page: Page): Promise<void> {
  await page.click('[data-testid="assistant"]');
  await page.waitForSelector('[data-testid="assistant-panel"]');
}

async function stream(page: Page, pieces: string[]): Promise<void> {
  for (const text of pieces) await emit(page, 'assistant-delta', { path: FAKE_PATH, text });
}

async function done(page: Page, error: string | null = null): Promise<void> {
  await emit(page, 'assistant-done', {
    path: FAKE_PATH,
    usage: { input_tokens: 4211, output_tokens: 57 },
    error
  });
}

async function ask(page: Page, question: string): Promise<void> {
  await page.fill(INPUT, question);
  await page.click(SEND);
  await page.waitForSelector(MESSAGE);
}

/* ============================================================ setup */

describe('the assistant says what it needs before it costs anything', () => {
  it(
    'shows a setup card with the two ways to give it a key, and no composer',
    async () => {
      const page = await boot({ assistant: NO_KEY });
      await openAssistant(page);

      const reason = await page.textContent('[data-testid="assistant-reason"]');
      expect(reason).toContain('ANTHROPIC_API_KEY');
      expect(reason).toContain('anthropic-key');
      expect(reason).toContain('chmod 600');
      expect(await page.locator(INPUT).count()).toBe(0);
      expect(await page.locator(CHIP).count()).toBe(0);
      expect(await page.textContent('[data-testid="assistant-setup"]')).toContain('costs money');
      await shot(page, 'assistant-setup');
      await page.close();
    },
    CASE
  );

  it(
    'never reaches the backend for an answer while it has no key',
    async () => {
      const page = await boot({ assistant: NO_KEY });
      await openAssistant(page);
      await page.click('[data-testid="assistant-recheck"]');

      expect((await calls(page)).filter((name) => name === 'assistant_status').length).toBe(2);
      expect(await asks(page)).toEqual([]);
      await page.close();
    },
    CASE
  );

  it(
    'names the model it will spend money on',
    async () => {
      const page = await boot();
      await openAssistant(page);

      expect(await page.textContent('[data-testid="assistant-model"]')).toContain(
        'claude-opus-5'
      );
      expect(await page.textContent('[data-testid="assistant-empty"]')).toContain('not saved');
      await page.close();
    },
    CASE
  );
});

/* ============================================================ starters */

describe('the starter chips follow the canvas', () => {
  it(
    'offers three starters and lights the third one only with a selection',
    async () => {
      const page = await boot();
      await openAssistant(page);

      expect(await page.locator(CHIP).count()).toBe(3);
      expect(await page.locator(`${CHIP}[data-chip="0"]`).textContent()).toContain(
        'Explain this flowgraph'
      );
      const third = page.locator(`${CHIP}[data-chip="2"]`);
      expect(await third.isDisabled()).toBe(true);
      expect(await third.getAttribute('title')).toContain('select a block');

      await page.click('.svelte-flow__node[data-id="source1"]');
      await expect.poll(() => third.textContent()).toBe('What does CWSource do?');
      expect(await third.isDisabled()).toBe(false);

      await page.click('.svelte-flow__node[data-id="throttle"]');
      await expect.poll(() => third.textContent()).toBe('What does Throttle do?');

      await shot(page, 'assistant-chips');
      await page.close();
    },
    CASE
  );

  it(
    'sends the selected block as the question when the third chip is taken',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await page.click('.svelte-flow__node[data-id="source1"]');
      await page.click(`${CHIP}[data-chip="2"]`);
      await page.waitForSelector(MESSAGE);

      const sent = (await asks(page)) as { path: string; question: string }[];
      expect(sent.length).toBe(1);
      expect(sent[0]?.path).toBe(FAKE_PATH);
      expect(sent[0]?.question).toContain('CWSource');
      expect(sent[0]?.question).toContain('var source1');
      await page.close();
    },
    CASE
  );
});

/* ============================================================ streaming */

describe('an answer streams in and is paid for in the open', () => {
  it(
    'renders deltas as they arrive, then the token cost',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await ask(page, 'what does this graph do?');

      expect(await page.locator(MESSAGE).count()).toBe(2);
      expect(await page.locator(`${MESSAGE}[data-role="user"]`).textContent()).toContain(
        'what does this graph do?'
      );

      await stream(page, ['chirp (chirp) ', 'feeds the throttle.']);
      const answer = page.locator(`${MESSAGE}[data-role="assistant"]`);
      await expect.poll(() => answer.textContent()).toContain('chirp (chirp) feeds the throttle.');
      expect(await page.locator(CARET).count()).toBe(1);

      await done(page);
      await expect.poll(() => page.locator(CARET).count()).toBe(0);
      expect(await page.textContent(USAGE)).toBe('4211 tokens in · 57 out');
      await shot(page, 'assistant-answer');
      await page.close();
    },
    CASE
  );

  it(
    'renders bold, code and lists without a markdown dependency',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await ask(page, 'how do I wire it?');
      await stream(page, ['**Wire** the `chirp` block:\n- to the throttle\n- then the plot\n']);
      const answer = page.locator(`${MESSAGE}[data-role="assistant"]`);
      await expect.poll(() => page.locator(CARET).count()).toBe(1);
      await done(page);

      expect(await answer.locator('strong').textContent()).toBe('Wire');
      expect(await answer.locator('code').textContent()).toBe('chirp');
      expect(await answer.locator('.line.bullet').count()).toBe(2);
      expect(await answer.locator('.line.bullet .marker').first().textContent()).toBe('•');
      await page.close();
    },
    CASE
  );

  it(
    'swaps send for stop while streaming and carries the history on the next question',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await ask(page, 'first question');

      expect(await page.locator(SEND).count()).toBe(0);
      expect(await page.locator(STOP).count()).toBe(1);

      await stream(page, ['first answer']);
      await done(page);
      await expect.poll(() => page.locator(SEND).count()).toBe(1);

      await ask(page, 'second question');
      const sent = (await asks(page)) as { history: { role: string; text: string }[] }[];
      expect(sent[1]?.history).toEqual([
        { role: 'user', text: 'first question' },
        { role: 'assistant', text: 'first answer' }
      ]);
      await page.close();
    },
    CASE
  );

  it(
    'stop ends the answer and tells the backend to hang up',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await ask(page, 'explain everything');
      await stream(page, ['it starts with ']);

      await page.click(STOP);

      await expect.poll(() => page.locator(SEND).count()).toBe(1);
      expect(await page.locator(CARET).count()).toBe(0);
      expect((await calls(page)).filter((name) => name === 'assistant_stop').length).toBe(1);

      await stream(page, ['LATE TEXT']);
      expect(await page.textContent(`${MESSAGE}[data-role="assistant"]`)).not.toContain(
        'LATE TEXT'
      );
      await page.close();
    },
    CASE
  );
});

/* ============================================================ failure */

describe('failures read as sentences', () => {
  it(
    'shows a streamed failure under the answer it belongs to',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await ask(page, 'why is this edge red?');
      await done(page, 'the Anthropic API is overloaded right now — try again in a moment');

      expect(await page.textContent('[data-testid="assistant-error"]')).toBe(
        'the Anthropic API is overloaded right now — try again in a moment'
      );
      expect(await page.locator(SEND).count()).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'shows a refused request as a sentence and frees the panel',
    async () => {
      const page = await boot({ askError: 'ask a question first' });
      await openAssistant(page);
      await ask(page, 'anything');

      await expect
        .poll(() => page.textContent('[data-testid="assistant-error"]'))
        .toBe('ask a question first');
      expect(await page.locator(SEND).count()).toBe(1);
      await page.close();
    },
    CASE
  );
});

/* ============================================================ the rail */

describe('the assistant shares the inspector rail', () => {
  it(
    'Ctrl+J opens the assistant tab and closes the rail again',
    async () => {
      const page = await boot();
      await page.keyboard.press('Control+j');
      await page.waitForSelector('[data-testid="assistant-panel"]');
      expect(await page.locator('.inspector').count()).toBe(0);
      expect(
        await page.locator('[data-testid="rail-tab-assistant"]').getAttribute('aria-selected')
      ).toBe('true');

      await page.keyboard.press('Control+j');
      await expect
        .poll(() => page.locator('[data-testid="assistant-panel"].collapsed').count())
        .toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'keeps the conversation while the inspector is used, and drops it with the file',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await ask(page, 'remember this');
      await stream(page, ['remembered']);
      await done(page);

      await page.click('[data-testid="rail-tab-inspector"]');
      await page.waitForSelector('.inspector');
      await page.click('[data-testid="rail-tab-assistant"]');
      expect(await page.locator(MESSAGE).count()).toBe(2);

      await page.click('aside.sidebar button.primary');
      await expect.poll(() => page.locator(MESSAGE).count()).toBe(0);
      expect(await page.locator('[data-testid="assistant-empty"]').count()).toBe(1);
      await page.close();
    },
    CASE
  );
});
