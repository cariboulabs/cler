import { describe, expect, it } from 'vitest';
import type { Page } from 'playwright';
import {
  asks,
  bases,
  boot,
  CASE,
  calls,
  commands,
  emit,
  FAKE_PATH,
  NO_KEY,
  peeks,
  settings,
  shot,
  useBrowser
} from './ui';

useBrowser();

const CARET = '[data-testid="assistant-caret"]';
const CHIP = '[data-testid="assistant-chip"]';
const INPUT = '[data-testid="assistant-input"]';
const MESSAGE = '[data-testid="assistant-message"]';
const STOP = '[data-testid="assistant-stop"]';
const USAGE = '[data-testid="assistant-usage"]';

async function openAssistant(page: Page): Promise<void> {
  await page.click('[data-testid="rail-tab-assistant"]');
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
  await page.press(INPUT, 'Enter');
  await page.waitForSelector(MESSAGE);
}

/* ============================================================ setup */

describe('the assistant says what it needs before it costs anything', () => {
  it(
    'shows a setup card with the two ways to give it a key, and no composer',
    async () => {
      const page = await boot({ assistant: NO_KEY });
      await openAssistant(page);

      expect(await page.locator('[data-testid="assistant-signin"]').count()).toBe(1);
      expect(await page.locator(INPUT).count()).toBe(0);
      expect(await page.locator(CHIP).count()).toBe(0);
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

      expect((await calls(page)).filter((name) => name === 'assistant_status').length).toBe(1);
      expect(await asks(page)).toEqual([]);
      await page.close();
    },
    CASE
  );

  it(
    'offers speed and cost, never a vendor model name',
    async () => {
      const page = await boot();
      await openAssistant(page);

      expect(
        await page.locator('[data-testid="assistant-model-select"]').inputValue()
      ).toBe('claude-opus-5');
      const labels = await page.locator('[data-testid="assistant-model-select"] option').allTextContents();
      expect(labels).toEqual(['Fast — cheapest', 'Balanced', 'Deep — most capable']);
      expect(labels.join(' ')).not.toContain('claude');
      await page.close();
    },
    CASE
  );

  it(
    'switching provider stores the choice, drops the stale model and stops the answer',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await ask(page, 'what is this graph?');

      await page.selectOption('[data-testid="assistant-provider-select"]', 'openai');
      await expect
        .poll(async () => (await calls(page)).filter((name) => name === 'assistant_stop').length)
        .toBe(1);
      const stored = (await settings(page)).at(-1) as Record<string, unknown>;
      expect(stored.aiAgentProvider).toBe('openai');
      expect(stored.assistantModel).toBe(null);
      await page.close();
    },
    CASE
  );
});

/* ============================================================ oauth */

describe('signing in with Claude is the front door', () => {
  it(
    'the sign-in button starts the browser flow and shows it is waiting',
    async () => {
      const page = await boot({ assistant: NO_KEY });
      await openAssistant(page);

      await page.click('[data-testid="assistant-signin"]');
      await expect
        .poll(() => page.textContent('[data-testid="assistant-signin"]'))
        .toContain('waiting for the browser');
      expect((await calls(page)).filter((name) => name === 'assistant_oauth_start').length).toBe(
        1
      );
      await shot(page, 'assistant-oauth-waiting');
      await page.close();
    },
    CASE
  );

  it(
    'the model can be switched and the choice goes to settings',
    async () => {
      const page = await boot();
      await openAssistant(page);

      await page.selectOption('[data-testid="assistant-model-select"]', 'claude-sonnet-5');
      await expect
        .poll(async () => (await calls(page)).filter((name) => name === 'set_app_settings').length)
        .toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'the browser completing on its own flips the panel through the auth event',
    async () => {
      const page = await boot({ assistant: NO_KEY });
      await openAssistant(page);

      await emit(page, 'assistant-auth-changed', {
        available: true,
        model: 'claude-opus-5',
        reason: null,
        method: 'oauth'
      });
      await page.waitForSelector('[data-testid="assistant-setup"]', { state: 'detached' });
      expect(await page.locator('[data-testid="assistant-logout"]').count()).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'sign out shows only for oauth and tells the backend to forget the tokens',
    async () => {
      const keyed = await boot();
      await openAssistant(keyed);
      expect(await keyed.locator('[data-testid="assistant-logout"]').count()).toBe(0);
      await keyed.close();

      const page = await boot({
        assistant: {
          available: true,
          provider: 'anthropic',
          model: 'claude-opus-5',
          reason: null,
          method: 'oauth'
        }
      });
      await openAssistant(page);
      await page.click('[data-testid="assistant-logout"]');
      await page.waitForSelector('[data-testid="assistant-setup"]');

      expect((await calls(page)).filter((name) => name === 'assistant_oauth_logout').length).toBe(
        1
      );
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
    'shows stop while streaming and carries the history on the next question',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await ask(page, 'first question');

      expect(await page.locator(STOP).count()).toBe(1);

      await stream(page, ['first answer']);
      await done(page);
      await expect.poll(() => page.locator(STOP).count()).toBe(0);

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

      await expect.poll(() => page.locator(STOP).count()).toBe(0);
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
      expect(await page.locator(STOP).count()).toBe(0);
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
      expect(await page.locator(STOP).count()).toBe(0);
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

      await page.click('[data-testid="file-menu"]');
      await page.click('[data-testid="file-open"]');
      await expect.poll(() => page.locator(MESSAGE).count()).toBe(0);
      await page.close();
    },
    CASE
  );
});

/* ============================================================ proposals */

const ACCEPT = '[data-testid="proposal-accept"]';
const CARD = '[data-testid="assistant-proposal"]';
const DIFF = '[data-testid="proposal-diff"]';
const REJECT = '[data-testid="proposal-reject"]';

const RENAME = [{ command: 'set_display_name', site: 0, block: 'source1', new_text: 'Chirp' }];

const REWIRE = [
  { command: 'disconnect', site: 0, edge: 3 },
  { command: 'connect', site: 0, from: 'adder', to: 'plot', port: 'in', port_index: 0 }
];

const SPARSE = [
  { command: 'add_block', site: 0, type: 'GuiManager', var_name: 'gui2' },
  { command: 'connect', site: 0, from: 'adder', to: 'throttle', port: 'in' },
  { command: 'define_block', site: 0, name: 'GainBlock', value_type: 'float' }
];

async function propose(
  page: Page,
  sent: unknown[] = RENAME,
  rationale = 'rename source1 so the legend reads right',
  dropped = 0
): Promise<void> {
  const before = await page.locator(CARD).count();
  await emit(page, 'assistant-proposal', {
    path: FAKE_PATH,
    rationale,
    commands: sent,
    dropped
  });
  await expect.poll(() => page.locator(CARD).count()).toBe(before + 1);
}

describe('a proposal is a checked diff the user accepts or rejects', () => {
  it(
    'renders the rationale, the commands in words, and the checked diff',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await ask(page, 'rename the first source');
      await stream(page, ['Renaming source1.']);
      await done(page);

      await propose(page);

      expect(await page.textContent('[data-testid="proposal-rationale"]')).toBe(
        'rename source1 so the legend reads right'
      );
      expect(await page.locator('[data-testid="proposal-command"]').textContent()).toBe(
        'rename source1 to "Chirp"'
      );
      expect(await page.locator(`${DIFF} .row.del`).textContent()).toContain('"CWSource"');
      expect(await page.locator(`${DIFF} .row.add`).textContent()).toContain('"Chirp"');
      expect(await page.locator(`${DIFF} .row.meta`).textContent()).toContain('@@');

      const checked = (await peeks(page)) as { commands: unknown[]; baseRevision: number }[];
      expect(checked.length).toBe(1);
      expect(checked[0]?.commands).toEqual(RENAME);
      expect(checked[0]?.baseRevision).toBe(1);
      expect(await commands(page)).toEqual([]);

      expect(await page.locator(ACCEPT).count()).toBe(1);
      expect(await page.locator(REJECT).count()).toBe(1);
      await shot(page, 'assistant-proposal');
      await page.close();
    },
    CASE
  );

  it(
    'says every command of a multi-command change in the vocabulary of the editor',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await propose(page, REWIRE, 'route the adder straight into the plot');

      expect(await page.locator('[data-testid="proposal-command"]').allTextContents()).toEqual([
        'disconnect edge 3',
        'connect adder → plot.in[0]'
      ]);
      await page.close();
    },
    CASE
  );

  it(
    'reads commands whose optional fields the model left out',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await propose(page, SPARSE, 'add a gui, wire the adder, define a gain block');

      expect(await page.locator('[data-testid="proposal-command"]').allTextContents()).toEqual([
        'add GuiManager as gui2',
        'connect adder → throttle.in',
        'define block GainBlock with 0 inputs and 0 outputs'
      ]);
      expect(await page.locator(ACCEPT).count()).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'accept applies exactly those commands at the revision it was checked against',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await propose(page);

      await page.click(ACCEPT);
      await page.waitForSelector('[data-testid="proposal-applied"]');

      expect(await commands(page)).toEqual(RENAME);
      expect(await bases(page)).toEqual([1]);
      expect(await page.textContent('[data-testid="proposal-applied"]')).toContain('revision 2');
      expect(await page.locator(ACCEPT).count()).toBe(0);
      expect(await page.locator(REJECT).count()).toBe(0);
      await shot(page, 'assistant-proposal-applied');
      await page.close();
    },
    CASE
  );

  it(
    'reject settles the card in the history and sends nothing',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await propose(page);

      await page.click(REJECT);
      await page.waitForSelector('[data-testid="proposal-rejected"]');

      expect(await commands(page)).toEqual([]);
      expect((await calls(page)).filter((name) => name === 'apply_commands').length).toBe(0);
      expect(await page.locator(ACCEPT).count()).toBe(0);
      expect(await page.locator(CARD).count()).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'a proposal the validator refuses shows why and offers no Accept',
    async () => {
      const page = await boot({
        previewError: JSON.stringify({
          error: 'references_outside_graph',
          block: 'plot',
          spans: [
            { start: 10, end: 20 },
            { start: 30, end: 40 }
          ]
        })
      });
      await openAssistant(page);
      await propose(page, [{ command: 'delete_block', site: 0, block: 'plot' }], 'drop the plot');

      const reason = await page.textContent('[data-testid="proposal-refusal"]');
      expect(reason).toContain('plot is still used in 2 places outside the flowgraph');
      expect(await page.locator(ACCEPT).count()).toBe(0);
      expect(await page.locator(DIFF).count()).toBe(0);
      expect(await page.locator('[data-testid="proposal-command"]').textContent()).toBe(
        'delete plot and its declaration'
      );

      const listed = page.locator('[data-testid="delete-refusal"]');
      expect(await listed.locator('button[data-reference]').count()).toBe(2);
      expect(await listed.textContent()).toContain('plot cannot be deleted');
      await shot(page, 'assistant-proposal-refused');
      await page.close();
    },
    CASE
  );

  it(
    'a card whose graph moved on goes stale, blocks accept, and re-checks on demand',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await propose(page);
      await propose(page, REWIRE, 'route the adder straight into the plot');

      const second = page.locator(CARD).nth(1);
      await page.locator(CARD).nth(0).locator(ACCEPT).click();
      await page.waitForSelector('[data-testid="proposal-applied"]');

      await expect.poll(() => second.locator('[data-testid="proposal-stale"]').count()).toBe(1);
      expect(await second.locator(ACCEPT).count()).toBe(0);
      expect(await second.locator('[data-testid="proposal-recheck"]').count()).toBe(1);

      await second.locator('[data-testid="proposal-recheck"]').click();
      await expect.poll(() => second.locator(ACCEPT).count()).toBe(1);

      const checked = (await peeks(page)) as { baseRevision: number }[];
      expect(checked.map((entry) => entry.baseRevision)).toEqual([1, 1, 2]);
      expect(await commands(page)).toEqual(RENAME);

      await second.locator(ACCEPT).click();
      await expect.poll(() => second.locator('[data-testid="proposal-applied"]').count()).toBe(1);
      expect(await bases(page)).toEqual([1, 2]);
      await page.close();
    },
    CASE
  );

  it(
    'names the one-proposal-per-answer ceiling when the model called the tool twice',
    async () => {
      const page = await boot();
      await openAssistant(page);
      await propose(page, RENAME, 'rename source1', 1);

      expect(await page.textContent('[data-testid="proposal-dropped"]')).toContain(
        'one proposal per answer'
      );
      await page.close();
    },
    CASE
  );

});
