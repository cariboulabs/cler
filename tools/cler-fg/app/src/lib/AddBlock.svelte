<script lang="ts">
  import { useSvelteFlow } from '@xyflow/svelte';
  import {
    addForm,
    addGaps,
    categoryOf,
    ctorSignature,
    initialBlockArguments,
    portsSummary,
    renameInForm,
    searchSpecs,
    suggestVarName,
    type AddField,
    type AddForm,
    type BlockSpec,
    type FieldRefusal
  } from './palette';
  import type { EdgePoint } from './project';

  type Props = {
    specs: BlockSpec[];
    documentPath: string;
    taken: string[];
    onadd: (spec: BlockSpec, form: AddForm, at: EdgePoint) => Promise<FieldRefusal | null>;
  };

  const { specs, documentPath, taken, onadd }: Props = $props();

  const flow = useSvelteFlow();
  const POPOVER_WIDTH = 316;
  const POPOVER_HEIGHT = 436;

  let at = $state<{ x: number; y: number } | null>(null);
  let spot = $state<EdgePoint>({ x: 0, y: 0 });
  let chosen = $state<BlockSpec | null>(null);
  let form = $state<AddForm | null>(null);
  let query = $state('');
  let refusal = $state<FieldRefusal | null>(null);
  let busy = $state(false);
  let attempted = $state(false);
  let opener: HTMLElement | null = null;

  const choices = $derived(chosen ? [] : searchSpecs(specs, query, documentPath));
  const gaps = $derived(form ? addGaps(form) : []);

  function canvasPane(): HTMLElement | null {
    return document.querySelector<HTMLElement>('[data-canvas]');
  }

  function focused(): HTMLElement | null {
    const active = document.activeElement;
    return active instanceof HTMLElement && active !== document.body ? active : null;
  }

  function restoreFocus() {
    const target = opener?.isConnected ? opener : canvasPane();
    opener = null;
    target?.focus();
  }

  export function openAt(clientX: number, clientY: number, spec?: BlockSpec): void {
    opener = focused();
    spot = flow.screenToFlowPosition({ x: clientX, y: clientY });
    at = {
      x: Math.min(clientX, window.innerWidth - POPOVER_WIDTH),
      y: Math.min(clientY, window.innerHeight - POPOVER_HEIGHT)
    };
    query = '';
    refusal = null;
    busy = false;
    attempted = false;
    chosen = null;
    form = null;
    if (spec) choose(spec);
  }

  export function close(): void {
    at = null;
    chosen = null;
    form = null;
    refusal = null;
    attempted = false;
    restoreFocus();
  }

  function choose(spec: BlockSpec) {
    chosen = spec;
    form = addForm(spec, suggestVarName(spec.name, taken));
    refusal = null;
    attempted = false;
  }

  function initialForm(spec: BlockSpec): AddForm {
    const base = addForm(spec, suggestVarName(spec.name, taken));
    const initial = initialBlockArguments(spec);
    return {
      ...base,
      templateArgs: base.templateArgs.map((field, index) => ({
        ...field,
        value: field.value || initial.templateArgs[index] || ''
      })),
      ctorArgs: base.ctorArgs.map((field, index) => ({
        ...field,
        value: field.value || initial.ctorArgs[index] || ''
      }))
    };
  }

  export function placeAt(clientX: number, clientY: number, spec: BlockSpec): void {
    const next = initialForm(spec);
    const position = flow.screenToFlowPosition({ x: clientX, y: clientY });
    void onadd(spec, next, position).then((outcome) => {
      if (!outcome) return;
      openAt(clientX, clientY, spec);
      form = next;
      refusal = outcome;
    });
  }

  function setVarName(text: string) {
    if (form) form = renameInForm(form, text);
  }

  function setField(kind: 'template' | 'ctor', index: number, text: string) {
    if (!form) return;
    const patch = (fields: AddField[]) =>
      fields.map((field, at) => (at === index ? { ...field, value: text } : field));
    form =
      kind === 'template'
        ? { ...form, templateArgs: patch(form.templateArgs) }
        : { ...form, ctorArgs: patch(form.ctorArgs) };
  }

  async function confirm() {
    if (!chosen || !form || busy) return;
    if (gaps.length > 0) {
      attempted = true;
      return;
    }
    busy = true;
    const outcome = await onadd(chosen, form, spot);
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
    if (event.key === 'Enter' && form) {
      void confirm();
      event.preventDefault();
    }
  }

  function errorFor(id: string): string | null {
    if (refusal?.field === id) return refusal.message;
    if (!attempted) return null;
    return gaps.find((gap) => gap.field === id)?.message ?? null;
  }
</script>

{#snippet row(
  id: string,
  label: string,
  hint: string,
  value: string,
  required: boolean,
  onset: (text: string) => void
)}
  <label class="field">
    <span class="label"
      >{label}{#if required}<span class="req" data-add-required={id}>*</span>{/if}<span class="hint"
        >{hint}</span
      ></span
    >
    <input
      type="text"
      data-add-field={id}
      {value}
      spellcheck="false"
      oninput={(event) => onset(event.currentTarget.value)}
    />
    {#if errorFor(id)}
      <span class="err" data-add-error={id}>{errorFor(id)}</span>
    {/if}
  </label>
{/snippet}

{#if at}
  <div
    class="popover nodrag nopan"
    role="dialog"
    tabindex="-1"
    aria-label="Add block"
    data-testid="add-block"
    style="left: {at.x}px; top: {at.y}px"
    onkeydown={onKeydown}
  >
    {#if chosen && form}
      <header>
        <span class="title">{chosen.name}</span>
        <span class="cat">{categoryOf(chosen, documentPath)}</span>
      </header>
      <code class="sig">{ctorSignature(chosen)}</code>
      <div class="fields">
        {@render row('var_name', 'variable', 'C++ name', form.varName, true, setVarName)}
        {#each form.templateArgs as field, index (field.id)}
          {@render row(field.id, field.label, field.hint, field.value, field.required, (text) =>
            setField('template', index, text)
          )}
        {/each}
        {#each form.ctorArgs as field, index (field.id)}
          {@render row(field.id, field.label, field.hint, field.value, field.required, (text) =>
            setField('ctor', index, text)
          )}
        {/each}
      </div>
      {#if refusal && refusal.field === null}
        <p class="err" data-add-error="form">{refusal.message}</p>
      {/if}
      <p class="note">declared unwired — wire it to put it in the graph</p>
      <footer>
        {#if gaps.length > 0}
          <span class="why" data-testid="add-why">
            {gaps.length} required field{gaps.length === 1 ? '' : 's'} to fill
          </span>
        {/if}
        <button data-testid="add-cancel" onclick={close}>Cancel</button>
        <button
          class="primary"
          data-testid="add-confirm"
          disabled={busy || gaps.length > 0}
          title={gaps.length > 0 ? 'every argument without a default is required' : undefined}
          onclick={confirm}
        >
          Add
        </button>
      </footer>
    {:else}
      <header><span class="title">Add block here</span></header>
      <!-- svelte-ignore a11y_autofocus -->
      <input
        type="search"
        autofocus
        placeholder="search blocks"
        data-testid="add-search"
        bind:value={query}
      />
      <ul>
        {#each choices as spec (spec.origin + spec.name)}
          <li>
            <button data-add-pick={spec.name} onclick={() => choose(spec)}>
              <span class="name">{spec.name}</span>
              <span class="cat">{categoryOf(spec, documentPath)}</span>
              <span class="ports">{portsSummary(spec)}</span>
            </button>
          </li>
        {:else}
          <li class="note">nothing matches “{query}”</li>
        {/each}
      </ul>
      <footer>
        <button data-testid="add-cancel" onclick={close}>Cancel</button>
      </footer>
    {/if}
  </div>
{/if}

<style>
  .popover {
    position: fixed;
    z-index: 30;
    width: 300px;
    max-height: 420px;
    overflow-y: auto;
    padding: var(--sp-2);
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
    font-size: 12px;
    font-weight: 600;
  }
  .cat,
  .ports {
    font-size: 11px;
    color: var(--muted);
  }
  .ports {
    margin-left: auto;
    font-family: var(--mono);
  }
  .sig {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
    word-break: break-all;
  }
  .fields {
    display: flex;
    flex-direction: column;
    gap: var(--sp-1);
  }
  .field {
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
  .req {
    padding-left: var(--sp-0);
    color: var(--danger-fg);
  }
  input {
    width: 100%;
    background: var(--bg-2);
    color: var(--fg);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: var(--sp-1) var(--sp-2);
    font-family: var(--mono);
    font-size: 11px;
  }
  input:focus {
    outline: none;
    border-color: var(--accent-hi);
  }
  ul {
    margin: 0;
    padding: 0;
    list-style: none;
    max-height: 220px;
    overflow-y: auto;
  }
  ul button {
    display: flex;
    align-items: baseline;
    gap: var(--sp-1);
    width: 100%;
    padding: var(--sp-0) var(--sp-1);
    background: transparent;
    border-color: transparent;
    text-align: left;
  }
  ul button:hover {
    background: var(--bg-2);
    border-color: transparent;
  }
  .name {
    font-size: 12px;
  }
  footer {
    display: flex;
    align-items: center;
    justify-content: flex-end;
    gap: var(--sp-2);
  }
  .why {
    margin-right: auto;
    font-size: 11px;
    color: var(--muted);
  }
  footer button {
    flex: none;
    width: auto;
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
</style>
