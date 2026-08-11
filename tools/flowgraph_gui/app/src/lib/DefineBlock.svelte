<script lang="ts">
  import {
    defineGaps,
    definePreview,
    emptyDefineForm,
    inputField,
    MAX_OUTPUTS,
    paletteTypes,
    paramField,
    type DefineForm,
    type DefinePreview
  } from './define';
  import type { BlockSpec, FieldRefusal } from './palette';

  type Props = {
    specs: BlockSpec[];
    documentPath: string;
    ondefine: (form: DefineForm) => Promise<FieldRefusal | null>;
  };

  const { specs, documentPath, ondefine }: Props = $props();

  const PREVIEW_MS = 150;
  const REQUIRED = 'required';
  const UNFILLED = 'a name, a value type and a type on every parameter are required';

  let shown = $state(false);
  let form = $state<DefineForm>(emptyDefineForm());
  let refusal = $state<FieldRefusal | null>(null);
  let attempted = $state(false);
  let busy = $state(false);
  let settled = $state.raw<DefinePreview | null>(null);
  let opener: HTMLElement | null = null;

  const gaps = $derived(defineGaps(form, specs, documentPath));
  const types = $derived(paletteTypes(specs));
  const blocked = $derived.by(() => {
    const first = gaps[0]?.message;
    if (first === undefined) return undefined;
    return first === REQUIRED ? UNFILLED : first;
  });

  $effect(() => {
    const wanted = definePreview(form);
    const timer = setTimeout(() => (settled = wanted), PREVIEW_MS);
    return () => clearTimeout(timer);
  });

  export function open(): void {
    const active = document.activeElement;
    opener = active instanceof HTMLElement && active !== document.body ? active : null;
    form = emptyDefineForm();
    refusal = null;
    attempted = false;
    busy = false;
    settled = null;
    shown = true;
  }

  export function close(): void {
    shown = false;
    refusal = null;
    attempted = false;
    const target = opener?.isConnected ? opener : null;
    opener = null;
    target?.focus();
  }

  function patch(next: Partial<DefineForm>) {
    form = { ...form, ...next };
    refusal = null;
  }

  function setInput(index: number, text: string) {
    patch({ inputs: form.inputs.map((input, at) => (at === index ? { name: text } : input)) });
  }

  function addInput() {
    patch({ inputs: [...form.inputs, { name: '' }] });
  }

  function dropInput(index: number) {
    patch({ inputs: form.inputs.filter((_, at) => at !== index) });
  }

  function setParam(index: number, part: 'name' | 'cppType' | 'default', text: string) {
    patch({
      params: form.params.map((param, at) => (at === index ? { ...param, [part]: text } : param))
    });
  }

  function addParam() {
    patch({ params: [...form.params, { name: '', cppType: 'float', default: '' }] });
  }

  function dropParam(index: number) {
    patch({ params: form.params.filter((_, at) => at !== index) });
  }

  function setOutputs(count: number) {
    patch({ outputs: Math.min(MAX_OUTPUTS, Math.max(0, count)) });
  }

  async function confirm() {
    if (busy) return;
    if (gaps.length > 0) {
      attempted = true;
      return;
    }
    busy = true;
    const outcome = await ondefine(form);
    busy = false;
    refusal = outcome;
    if (!outcome) close();
  }

  function onKeydown(event: KeyboardEvent) {
    if (event.key === 'Escape') {
      close();
      event.stopPropagation();
      return;
    }
    if (event.key === 'Enter' && !(event.target instanceof HTMLButtonElement)) {
      void confirm();
      event.preventDefault();
    }
  }

  function errorFor(id: string): string | null {
    if (refusal?.field === id) return refusal.message;
    const gap = gaps.find((entry) => entry.field === id)?.message ?? null;
    if (gap === null || attempted) return gap;
    return gap === REQUIRED ? null : gap;
  }
</script>

{#snippet slot(id: string, label: string, hint: string, value: string, onset: (text: string) => void)}
  <label class="field">
    <span class="label">{label}<span class="hint">{hint}</span></span>
    <input
      type="text"
      data-define-field={id}
      {value}
      spellcheck="false"
      oninput={(event) => onset(event.currentTarget.value)}
    />
  </label>
{/snippet}

{#snippet oops(id: string)}
  {#if errorFor(id)}
    <span class="err" data-define-error={id}>{errorFor(id)}</span>
  {/if}
{/snippet}

{#if shown}
  <div class="scrim" data-testid="define-scrim"></div>
  <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
  <div
    class="card"
    role="dialog"
    aria-modal="true"
    aria-label="New block type"
    tabindex="-1"
    data-testid="define-block"
    onkeydown={onKeydown}
  >
    <header>
      <span class="title">New block type</span>
      <span class="cat">this file</span>
    </header>

    <div class="fields">
      <label class="field">
        <span class="label">type name<span class="hint">must end in Block</span></span>
        <!-- svelte-ignore a11y_autofocus -->
        <input
          type="text"
          autofocus
          placeholder="MyGainBlock"
          data-define-field="name"
          value={form.name}
          spellcheck="false"
          oninput={(event) => patch({ name: event.currentTarget.value })}
        />
      </label>
      {@render oops('name')}

      <label class="field">
        <span class="label">value type<span class="hint">channel element</span></span>
        <input
          type="text"
          list="cler-value-types"
          data-define-field="value_type"
          value={form.valueType}
          spellcheck="false"
          oninput={(event) => patch({ valueType: event.currentTarget.value })}
        />
      </label>
      <datalist id="cler-value-types">
        {#each types as name (name)}
          <option value={name}></option>
        {/each}
      </datalist>
      {@render oops('value_type')}
    </div>

    <section>
      <h3>
        Inputs<button data-testid="define-add-input" onclick={addInput}>+ input</button>
      </h3>
      {#each form.inputs as input, index (index)}
        <div class="row" data-define-input={index}>
          {@render slot(inputField(index), 'port', 'channel member', input.name, (text) =>
            setInput(index, text)
          )}
          <button
            class="drop"
            data-define-drop-input={index}
            aria-label="Remove input"
            onclick={() => dropInput(index)}>×</button
          >
        </div>
        {@render oops(inputField(index))}
      {:else}
        <p class="note">no inputs — this block is a source</p>
      {/each}
    </section>

    <section>
      <h3>Outputs</h3>
      <div class="stepper">
        <button data-testid="define-outputs-down" aria-label="One fewer output" onclick={() => setOutputs(form.outputs - 1)}
          >−</button
        >
        <output data-testid="define-outputs">{form.outputs}</output>
        <button data-testid="define-outputs-up" aria-label="One more output" onclick={() => setOutputs(form.outputs + 1)}
          >+</button
        >
        <span class="note">procedure() takes one ChannelBase* per output</span>
      </div>
      {@render oops('outputs')}
    </section>

    <section>
      <h3>
        Parameters<button data-testid="define-add-param" onclick={addParam}>+ parameter</button>
      </h3>
      {#each form.params as param, index (index)}
        <div class="row" data-define-param={index}>
          {@render slot(paramField(index, 'name'), 'name', '', param.name, (text) =>
            setParam(index, 'name', text)
          )}
          {@render slot(paramField(index, 'type'), 'type', '', param.cppType, (text) =>
            setParam(index, 'cppType', text)
          )}
          {@render slot(paramField(index, 'default'), 'default', 'optional', param.default, (text) =>
            setParam(index, 'default', text)
          )}
          <button
            class="drop"
            data-define-drop-param={index}
            aria-label="Remove parameter"
            onclick={() => dropParam(index)}>×</button
          >
        </div>
        {@render oops(paramField(index, 'name'))}
        {@render oops(paramField(index, 'type'))}
        {@render oops(paramField(index, 'default'))}
      {:else}
        <p class="note">no constructor parameters beyond the display name</p>
      {/each}
    </section>

    <label class="check">
      <input
        type="checkbox"
        data-define-field="may_block"
        checked={form.mayBlock}
        onchange={(event) => patch({ mayBlock: event.currentTarget.checked })}
      />
      <span>may_block — this block parks its worker instead of spinning</span>
    </label>

    <section class="preview" data-testid="define-preview">
      <h3>Preview</h3>
      {#if settled}
        <code data-testid="define-preview-ports">{settled.ports}</code>
        <code data-testid="define-preview-ctor">{settled.ctor}</code>
        <span class="note">{settled.note}</span>
      {:else}
        <span class="note">…</span>
      {/if}
    </section>

    {#if refusal && refusal.field === null}
      <p class="err" data-define-error="form">{refusal.message}</p>
    {/if}

    <footer>
      <button data-testid="define-cancel" onclick={close}>Cancel</button>
      <button
        class="primary"
        data-testid="define-confirm"
        disabled={busy || gaps.length > 0}
        title={blocked}
        onclick={confirm}
      >
        Create
      </button>
    </footer>
  </div>
{/if}

<style>
  .scrim {
    position: fixed;
    inset: 0;
    z-index: 29;
    background: var(--scrim);
  }
  .card {
    position: fixed;
    z-index: 30;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    width: min(520px, calc(100% - 2 * var(--sp-4)));
    max-height: calc(100% - 2 * var(--sp-4));
    overflow-y: auto;
    padding: var(--sp-3);
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border-hi);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
  }
  header {
    display: flex;
    align-items: baseline;
    gap: var(--sp-2);
  }
  .title {
    font-size: 13px;
    font-weight: 600;
  }
  .cat {
    font-size: 11px;
    color: var(--muted);
  }
  h3 {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    margin: 0 0 var(--sp-1);
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  h3 button {
    margin-left: auto;
    flex: none;
    width: auto;
    padding: 0 var(--sp-2);
    font-size: 11px;
    letter-spacing: 0;
    text-transform: none;
  }
  section {
    border-top: 1px solid var(--border);
    padding-top: var(--sp-2);
  }
  .fields,
  .row {
    display: flex;
    flex-direction: column;
    gap: var(--sp-1);
  }
  .row {
    flex-direction: row;
    align-items: flex-end;
    gap: var(--sp-2);
  }
  .field {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: var(--sp-0);
  }
  .label {
    display: flex;
    gap: var(--sp-2);
    font-size: 11px;
    color: var(--fg);
  }
  .hint {
    margin-left: auto;
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  input[type='text'] {
    width: 100%;
    background: var(--bg-2);
    color: var(--fg);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: var(--sp-1) var(--sp-2);
    font-family: var(--mono);
    font-size: 11px;
  }
  input[type='text']:focus {
    outline: none;
    border-color: var(--accent-hi);
  }
  .drop {
    flex: none;
    width: 24px;
    padding: var(--sp-1) 0;
    font-size: 13px;
    line-height: 1;
    color: var(--muted);
  }
  .stepper {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
  }
  .stepper button {
    flex: none;
    width: 26px;
    padding: var(--sp-0) 0;
    font-size: 13px;
    line-height: 1.1;
  }
  output {
    min-width: 22px;
    text-align: center;
    font-family: var(--mono);
    font-size: 12px;
    color: var(--fg);
  }
  .check {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    font-size: 11px;
    color: var(--fg);
  }
  .preview {
    display: flex;
    flex-direction: column;
    gap: var(--sp-0);
  }
  code {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--fg);
    word-break: break-all;
  }
  .note {
    margin: 0;
    font-size: 11px;
    color: var(--muted);
  }
  .err {
    font-size: 11px;
    color: var(--danger-fg);
  }
  p.err {
    margin: 0;
  }
  footer {
    display: flex;
    justify-content: flex-end;
    gap: var(--sp-2);
  }
  footer button {
    flex: none;
    width: auto;
  }
</style>
