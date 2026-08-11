import type { Page } from 'playwright';
import { describe, expect, it } from 'vitest';
import { fixtures } from '../src/fixtures';
import { blockFields, type Field } from '../src/lib/inspector';
import {
  addBlockCommand,
  addForm,
  addGaps,
  braceListLength,
  connectPlan,
  countOf,
  specFor,
  type AddField,
  type AddForm,
  type BlockSpec
} from '../src/lib/palette';
import { problemsOf, projectSite } from '../src/lib/project';
import type { Block, Site } from '../src/lib/schema';
import {
  bases,
  boot,
  calls,
  CASE,
  commands,
  dragWire,
  handle,
  hold,
  modelOf,
  openMenu,
  release,
  specs,
  useBrowser,
  wire
} from './ui';

useBrowser();

const SHRINK = '2 ports are wired — disconnect them before lowering this';
const LABEL_REFUSAL =
  'PlotTimeSeriesBlock sizes its ports from signal_labels, which holds 1 — add a label there first, then wire it';

function siteOf(name: string): Site {
  const site = structuredClone(fixtures[name])?.sites[0];
  if (!site) throw new Error(`no site in ${name}`);
  return site;
}

function specOf(name: string): BlockSpec {
  const found = specFor(specs, name);
  if (!found) throw new Error(`no spec ${name}`);
  return found;
}

function blockIn(site: Site, name: string): Block {
  const found = site.blocks.find((candidate) => candidate.var === name);
  if (!found) throw new Error(`no block ${name}`);
  return found;
}

function fieldOf(fields: Field[], id: string): Field {
  const found = fields.find((field) => field.id === id);
  if (!found) throw new Error(`no field ${id}`);
  return found;
}

function inputsOf(site: Site, name: string): { id: string; grow: boolean }[] {
  const node = projectSite(site, specs, true).nodes.find((candidate) => candidate.id === name);
  if (!node) throw new Error(`no node ${name}`);
  return node.data.inputs.map((slot) => ({ id: slot.id, grow: slot.grow }));
}

function filled(form: AddForm, values: Record<string, string>): AddForm {
  const patch = (fields: AddField[]) =>
    fields.map((field) => ({ ...field, value: values[field.id] ?? field.value }));
  return { ...form, templateArgs: patch(form.templateArgs), ctorArgs: patch(form.ctorArgs) };
}

function addCommand(spec: BlockSpec, form: AddForm) {
  const command = addBlockCommand(0, spec, form);
  if (command.command !== 'add_block') throw new Error('not an add_block command');
  return command;
}

function activeTag(page: Page): Promise<string> {
  return page.evaluate(() => document.activeElement?.tagName ?? 'none');
}

function activeIsCanvas(page: Page): Promise<boolean> {
  return page.evaluate(
    () =>
      document.activeElement instanceof HTMLElement &&
      document.activeElement.hasAttribute('data-canvas')
  );
}

/* ================================================ 1. required constructor arguments */

describe('1. an argument without a default is required, never dropped', () => {
  it('A1 SourceCWBlock names every gap instead of emitting a one-argument declaration', () => {
    const spec = specOf('SourceCWBlock');
    const form = addForm(spec, 'source_c_w');
    expect(form.templateArgs.map((field) => [field.label, field.required])).toEqual([['T', true]]);
    expect(form.ctorArgs.map((field) => [field.label, field.required])).toEqual([
      ['name', true],
      ['amplitude', true],
      ['frequency_hz', true],
      ['sps', true]
    ]);
    expect(addGaps(form)).toEqual([
      { field: 'template.0', message: 'required' },
      { field: 'ctor.1', message: 'required' },
      { field: 'ctor.2', message: 'required' },
      { field: 'ctor.3', message: 'required' }
    ]);

    const complete = filled(form, {
      'template.0': 'float',
      'ctor.1': '1.0f',
      'ctor.2': '2.0f',
      'ctor.3': '1000'
    });
    expect(addGaps(complete)).toEqual([]);
    expect(addCommand(spec, complete)).toEqual({
      command: 'add_block',
      site: 0,
      type: 'SourceCWBlock',
      template_args: ['float'],
      ctor_args: ['"source_c_w"', '1.0f', '2.0f', '1000'],
      var_name: 'source_c_w'
    });
  });

  it('A2 a blank gain_value is refused instead of sliding buffer_size into its slot', () => {
    const spec = specOf('GainBlock');
    const form = addForm(spec, 'gain');
    expect(form.ctorArgs.map((field) => field.value)).toEqual(['"gain"', '', '0']);
    expect(addGaps(form)).toEqual([
      { field: 'template.0', message: 'required' },
      { field: 'ctor.1', message: 'required' }
    ]);
    expect(addCommand(spec, form).ctor_args).toEqual(['"gain"', '', '0']);
  });

  it('a blank optional argument followed by a filled one is a field-level refusal', () => {
    const form: AddForm = {
      varName: 'block',
      templateArgs: [],
      ctorArgs: [
        { id: 'ctor.0', label: 'name', value: '"b"', hint: 'const char*', required: true },
        { id: 'ctor.1', label: 'middle', value: '', hint: 'size_t', required: false },
        { id: 'ctor.2', label: 'tail', value: '3', hint: 'size_t', required: false }
      ]
    };
    expect(addGaps(form)).toEqual([
      { field: 'ctor.1', message: 'fill this in — a later argument is set' }
    ]);
    expect(addGaps({ ...form, varName: '  ' })[0]).toEqual({
      field: 'var_name',
      message: 'required'
    });
  });

  it(
    'the popover keeps Add disabled until every required field is filled',
    async () => {
      const page = await boot();
      await openMenu(page, '.svelte-flow__pane', { x: 620, y: 620 });
      await page.click('[data-testid="menu-add-here"]');
      await page.fill('[data-testid="add-search"]', 'SourceCW');
      await page.click('[data-add-pick="SourceCWBlock"]');
      await page.waitForSelector('[data-testid="add-block"]');

      const confirm = page.locator('[data-testid="add-confirm"]');
      expect(await confirm.isDisabled()).toBe(true);
      expect(await page.locator('[data-add-required="ctor.3"]').count()).toBe(1);

      await page.press('[data-add-field="ctor.1"]', 'Enter');
      await expect.poll(() => page.textContent('[data-add-error="template.0"]')).toBe('required');
      expect(await page.textContent('[data-add-error="ctor.3"]')).toBe('required');
      expect(await commands(page)).toEqual([]);

      await page.fill('[data-add-field="template.0"]', 'float');
      await page.fill('[data-add-field="ctor.1"]', '1.0f');
      await page.fill('[data-add-field="ctor.2"]', '2.0f');
      await page.fill('[data-add-field="ctor.3"]', '1000');
      await expect.poll(() => confirm.isDisabled()).toBe(false);
      await confirm.click();

      await expect.poll(() => commands(page)).toEqual([
        {
          command: 'add_block',
          site: 0,
          type: 'SourceCWBlock',
          template_args: ['float'],
          ctor_args: ['"source_c_w"', '1.0f', '2.0f', '1000'],
          var_name: 'source_c_w'
        }
      ]);
      await page.close();
    },
    CASE
  );
});

/* ================================================ 2. arity shrink guard */

describe('2. an authority argument cannot drop below the wired count', () => {
  it('refuses lowering AddBlock<float, 2> while both inputs are wired', () => {
    const site = siteOf('hello_world');
    const fields = blockFields(0, blockIn(site, 'adder'), specOf('AddBlock'), site);
    const arity = fieldOf(fields, 'adder.template.1');
    expect(arity.refuse?.('1')).toBe(SHRINK);
    expect(arity.refuse?.('0')).toBe(SHRINK);
    expect(arity.refuse?.('2')).toBeNull();
    expect(arity.refuse?.('3')).toBeNull();
    expect(arity.refuse?.('kNumInputs')).toBeNull();
    expect(fieldOf(fields, 'adder.ctor.0').refuse).toBeUndefined();
    expect(fieldOf(fields, 'adder.display_name').refuse).toBeUndefined();
  });

  it('leaves an unwired block free to shrink', () => {
    const site = siteOf('hello_world');
    site.edges = [];
    const fields = blockFields(0, blockIn(site, 'adder'), specOf('AddBlock'), site);
    expect(fieldOf(fields, 'adder.template.1').refuse?.('1')).toBeNull();
  });

  it(
    'shows the refusal on the field and sends nothing',
    async () => {
      const page = await boot();
      await page.click('.svelte-flow__node[data-id="adder"]');
      const field = page.locator('input[data-field="adder.template.1"]');
      await field.fill('1');
      await field.press('Enter');

      await expect.poll(() => page.textContent('[data-error="adder.template.1"]')).toBe(SHRINK);
      expect(await commands(page)).toEqual([]);
      expect(await field.inputValue()).toBe('2');
      await page.close();
    },
    CASE
  );
});

/* ================================================ 3. string-aware brace lists */

describe('3. a label list is tokenised, not split on every comma', () => {
  it('B1/B2 commas and angle brackets inside a literal do not move the count', () => {
    expect(braceListLength('{"gain, dB"}')).toBe(1);
    expect(braceListLength('{"a>b", "c"}')).toBe(2);
    expect(braceListLength('{"a\\", b", "c"}')).toBe(2);
    expect(braceListLength('{"a", "b", "c"}')).toBe(3);
    expect(braceListLength('{{1, 2}, {3, 4}}')).toBe(2);
    expect(braceListLength('{}')).toBe(0);
    expect(braceListLength('labels')).toBeNull();
  });

  it('B3 the comma inside a label no longer reaches the canvas as a phantom port', () => {
    const site = siteOf('hello_world');
    const plot = blockIn(site, 'plot');
    const labels = plot.ctor_args[1];
    if (!labels) throw new Error('no signal_labels argument');
    labels.text = '{"gain, dB"}';

    expect(countOf(plot, specOf('PlotTimeSeriesBlock').input_count)).toBe(1);
    expect(inputsOf(site, 'plot')).toEqual([
      { id: 'in[0]', grow: false },
      { id: 'in[1]', grow: true }
    ]);
    expect(
      connectPlan(0, site, specs, { from: 'source2', to: 'plot', port: 'in', portIndex: 1 })
    ).toEqual({ refusal: LABEL_REFUSAL });
  });
});

/* ================================================ 4. the revision guard */

describe('4. a gesture carries the revision it was planned against', () => {
  it(
    'D1 two gestures fired before either lands both send the planning revision',
    async () => {
      const page = await boot();
      await hold(page);

      await page.click(wire('source1', 'adder'));
      await page.keyboard.press('Delete');
      await expect.poll(() => bases(page)).toEqual([1]);

      await page.click(wire('source2', 'adder'));
      await page.keyboard.press('Delete');
      await release(page);

      await expect.poll(() => bases(page)).toEqual([1, 1]);
      await page.close();
    },
    CASE
  );

  it(
    'a revision_mismatch says the graph moved and refreshes the document',
    async () => {
      const page = await boot({
        refusal: JSON.stringify({ error: 'revision_mismatch', base_revision: 1, current_revision: 4 })
      });
      await page.click('.svelte-flow__node[data-id="source2"]');
      await page.keyboard.press('Delete');

      const toast = page.locator('[data-testid="alert-toast"]');
      await toast.waitFor();
      expect(await toast.textContent()).toContain('the graph changed under this gesture — try again');
      await expect
        .poll(async () => (await calls(page)).filter((call) => call === 'open_document').length)
        .toBe(2);
      expect(await page.textContent('[data-testid="status"]')).toContain(
        'the graph changed under this gesture'
      );
      await page.close();
    },
    CASE
  );
});

/* ================================================ 5. refusals reach the top bar */

describe('5. a refusal is visible with the sidebar collapsed', () => {
  it(
    'U1 the reason lands in a danger toast at the top bar',
    async () => {
      const page = await boot();
      await page.click('[data-testid="toggle-left"]');
      await page.waitForTimeout(900);
      await dragWire(page, handle('source1', 'out'), handle('adder', 'in[1]'));

      const toast = page.locator('[data-testid="alert-toast"]');
      await toast.waitFor();
      expect(await toast.textContent()).toContain('adder.in[1] already takes source2');
      expect(await toast.isVisible()).toBe(true);
      expect(await toast.getAttribute('role')).toBe('alert');
      expect(await page.locator('[data-testid="status"]').isVisible()).toBe(false);
      expect(await commands(page)).toEqual([]);

      await page.click('[data-testid="alert-dismiss"]');
      await expect.poll(() => toast.count()).toBe(0);
      await page.close();
    },
    CASE
  );
});

/* ================================================ 6. focus after the popover closes */

describe('6. closing the add popover hands focus back', () => {
  it(
    'returns focus to the canvas and never leaves it on the body',
    async () => {
      const page = await boot();
      await openMenu(page, '.svelte-flow__pane', { x: 420, y: 320 });
      await page.click('[data-testid="menu-add-here"]');
      await page.waitForSelector('[data-testid="add-search"]');
      await page.keyboard.type('Gain');
      await page.keyboard.press('Escape');
      await page.waitForSelector('[data-testid="add-block"]', { state: 'detached' });
      expect(await activeTag(page)).not.toBe('BODY');
      expect(await activeIsCanvas(page)).toBe(true);
      await page.close();
    },
    CASE
  );
});

/* ================================================ 7. a block with no runner */

describe('7. a wired block with no runner is called out', () => {
  it('problemsOf reports it as a warning against the block', () => {
    const site = siteOf('hello_world');
    site.runners = site.runners.filter((runner) => runner.block !== 'adder');
    expect(
      problemsOf(site).map((problem) => [problem.id, problem.kind, problem.severity, problem.block])
    ).toEqual([['no-runner:adder', 'no runner', 'warning', 'adder']]);
    expect(blockIn(site, 'adder').in_graph).toBe(true);
    const node = projectSite(site, specs, true).nodes.find((candidate) => candidate.id === 'adder');
    expect(node?.data.hasRunner).toBe(false);
  });

  it(
    'U5 removing a block its consumers still read from hatches it and files a warning',
    async () => {
      const page = await boot();
      await openMenu(page, '.svelte-flow__node[data-id="adder"]');
      await page.click('[data-testid="menu-remove"]');

      const node = page.locator('.svelte-flow__node[data-id="adder"]');
      await expect.poll(() => node.locator('.block.unwired').count()).toBe(1);
      expect(await node.locator('.unwired-badge').textContent()).toBe('no runner');

      const chip = page.locator('[data-testid="problems"]');
      await expect.poll(() => chip.getAttribute('data-count')).toBe('1');
      expect(await chip.getAttribute('class')).not.toContain('danger');

      await page.click('.svelte-flow__node[data-id="source1"]');
      await expect.poll(() => page.textContent('.inspector .title')).toBe('CWSource');

      await chip.click();
      await page.click('[data-problem="no-runner:adder"]');
      await expect.poll(() => page.textContent('.inspector .title')).toBe('Adder');
      await page.close();
    },
    CASE
  );
});

/* ================================================ 8. example mode */

describe('8. examples are real editable documents in the desktop shell', () => {
  it(
    'G3/G5 opens the selected example and keeps editor actions enabled',
    async () => {
      const page = await boot();
      await page.selectOption('[data-testid="example-select"]', 'plots');
      await expect
        .poll(async () => (await calls(page)).filter((call) => call === 'open_document').length)
        .toBe(2);
      expect(await page.locator('[data-testid="demo-chip"]').count()).toBe(0);

      await openMenu(page, '.svelte-flow__node[data-id="source1"]');
      const editor = page.locator('[data-testid="menu-open-editor"]');
      expect(await editor.isDisabled()).toBe(false);
      await page.close();
    },
    CASE
  );
});
