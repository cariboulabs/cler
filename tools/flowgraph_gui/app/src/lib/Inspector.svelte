<script lang="ts">
  import {
    blockFields,
    blurAction,
    configFields,
    keyAction,
    type Field,
    type FieldAction
  } from './inspector';
  import { blockPorts, typeSignature } from './project';
  import type { Command, Site } from './schema';

  type Props = {
    site: Site | undefined;
    siteIndex: number;
    selected: string | null;
    enabled: boolean;
    submit: (command: Command) => Promise<string | null>;
    open: boolean;
    ontoggle: () => void;
  };

  const { site, siteIndex, selected, enabled, submit, open, ontoggle }: Props = $props();

  let drafts = $state<Record<string, string>>({});
  let errors = $state<Record<string, string>>({});

  const block = $derived(site?.blocks.find((candidate) => candidate.var === selected));
  const fields = $derived(block ? blockFields(siteIndex, block) : []);
  const config = $derived(site?.config ?? null);
  const configRows = $derived(config ? configFields(siteIndex, config) : []);
  const ports = $derived(site && block ? blockPorts(site, block.var) : null);
  const toggleTitle = $derived(
    open
      ? 'Collapse inspector  ]'
      : block
        ? `${block.display_name ?? block.var} selected — expand inspector  ]`
        : 'Expand inspector  ]'
  );

  function omit(map: Record<string, string>, id: string): Record<string, string> {
    return Object.fromEntries(Object.entries(map).filter(([key]) => key !== id));
  }

  async function run(field: Field, action: FieldAction) {
    if (action.kind === 'none') return;
    drafts = omit(drafts, field.id);
    errors = omit(errors, field.id);
    if (action.kind !== 'commit') return;
    const message = await submit(field.toCommand(action.text));
    if (message) errors = { ...errors, [field.id]: message };
  }

  function readable(text: string): string {
    return text.replace(/_/g, ' ');
  }
</script>

{#snippet fieldRow(field: Field)}
  <label class="field" class:ro={!field.editable}>
    <span class="label">{field.label}</span>
    <input
      type="text"
      data-field={field.id}
      value={drafts[field.id] ?? field.value}
      disabled={!enabled || !field.editable}
      title={field.hint ? readable(field.hint) : undefined}
      oninput={(event) => (drafts[field.id] = event.currentTarget.value)}
      onblur={() => run(field, blurAction(drafts[field.id], field.value))}
      onkeydown={(event) => run(field, keyAction(event.key, drafts[field.id], field.value))}
    />
    {#if errors[field.id]}
      <span class="err" data-error={field.id}>{errors[field.id]}</span>
    {:else if field.hint && field.hint !== block?.read_only_reason}
      <span class="hint">{readable(field.hint)}</span>
    {/if}
  </label>
{/snippet}

<aside class="inspector" class:collapsed={!open}>
  <div class="head">
    <button
      class="toggle"
      class:live={!open && !!block}
      data-testid="toggle-right"
      aria-expanded={open}
      aria-label={open ? 'Collapse inspector' : 'Expand inspector'}
      title={toggleTitle}
      onclick={ontoggle}
    >
      {#if open}
        ›
      {:else}
        <svg viewBox="0 0 16 16" width="15" height="15" fill="currentColor" aria-hidden="true">
          <path
            d="M2 4h6M13 4h1M2 8h1M8 8h6M2 12h4M11 12h3"
            fill="none"
            stroke="currentColor"
            stroke-width="1.5"
            stroke-linecap="round"
          />
          <circle cx="10" cy="4" r="1.7" />
          <circle cx="5" cy="8" r="1.7" />
          <circle cx="8" cy="12" r="1.7" />
        </svg>
      {/if}
    </button>
    <h1>Inspector</h1>
  </div>

  <div class="body">
    {#if block}
      <section>
        <h2>Block</h2>
        <div class="title">{block.display_name ?? block.var}</div>
        <div class="type">{typeSignature(block)}</div>
        <dl>
          <dt>var</dt>
          <dd>{block.var}</dd>
          {#if block.alias}
            <dt>alias</dt>
            <dd>{block.alias}</dd>
          {/if}
          <dt>in graph</dt>
          <dd>{block.in_graph ? 'yes' : 'declared only'}</dd>
        </dl>
        {#if !block.editable}
          <p class="reason" data-testid="block-reason">
            read-only: {readable(block.read_only_reason ?? 'unsupported form')}
          </p>
        {/if}
      </section>

      <section>
        <h2>Parameters</h2>
        <div class="fields">
          {#each fields as field (field.id)}
            {@render fieldRow(field)}
          {/each}
        </div>
      </section>

      {#if ports}
        <section>
          <h2>Ports</h2>
          {#if ports.inputs.length === 0 && ports.outputs.length === 0}
            <p class="muted">no wired ports</p>
          {:else}
            <ul class="ports">
              {#each ports.inputs as port (port)}
                <li><span class="dir">in</span><span class="port">{port}</span></li>
              {/each}
              {#each ports.outputs as port (port)}
                <li><span class="dir">out</span><span class="port">{port}</span></li>
              {/each}
            </ul>
          {/if}
        </section>
      {/if}
    {:else}
      <section>
        <h2>Block</h2>
        <p class="muted">click a block to edit its parameters</p>
      </section>
    {/if}

    {#if config}
      <section>
        <h2>Config <span class="source">{config.source}</span></h2>
        {#if configRows.length === 0}
          <p class="muted">no direct assignments</p>
        {:else}
          <div class="fields">
            {#each configRows as field (field.id)}
              {@render fieldRow(field)}
            {/each}
          </div>
        {/if}
      </section>
    {/if}

    {#if !enabled}
      <p class="muted" data-testid="viewer-note">
        fixture mode — read-only viewer, editing needs the desktop shell
      </p>
    {/if}
  </div>
</aside>

<style>
  .inspector {
    flex: none;
    width: 320px;
    background: var(--bg-1);
    border-left: 1px solid var(--border);
    overflow: hidden;
    display: flex;
    flex-direction: column;
    transition: width 150ms ease;
  }
  .inspector.collapsed {
    width: 44px;
  }
  .head {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    padding: var(--sp-3);
  }
  .collapsed .head {
    padding: var(--sp-2) 9px;
  }
  .toggle {
    flex: none;
    width: 26px;
    padding: 2px 0;
    font-size: 15px;
    line-height: 1.1;
    color: var(--muted);
  }
  .toggle svg {
    display: block;
    margin: 0 auto;
  }
  .toggle.live {
    border-color: var(--accent);
    color: var(--accent-hi);
  }
  h1 {
    margin: 0;
    font-size: 10px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--faint);
    font-weight: 600;
  }
  .collapsed h1 {
    display: none;
  }
  .body {
    flex: 1;
    min-height: 0;
    width: 320px;
    padding: 0 var(--sp-3) var(--sp-3);
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: var(--sp-4);
    transition:
      opacity 120ms ease,
      visibility 150ms;
  }
  .collapsed .body {
    opacity: 0;
    visibility: hidden;
  }
  h2 {
    margin: 0 0 var(--sp-2);
    font-size: 10px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--faint);
    font-weight: 600;
  }
  .source {
    text-transform: none;
    letter-spacing: 0;
    color: var(--muted);
    font-family: var(--mono);
  }
  .title {
    font-weight: 600;
    font-size: 14px;
  }
  .type {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
    word-break: break-all;
  }
  dl {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 1px var(--sp-3);
    margin: var(--sp-2) 0 0;
  }
  dt {
    color: var(--muted);
  }
  dd {
    margin: 0;
    font-family: var(--mono);
    font-size: 11px;
  }
  .reason {
    margin: var(--sp-2) 0 0;
    padding: var(--sp-1) var(--sp-2);
    border: 1px solid var(--danger-border);
    background: var(--danger-bg);
    border-radius: var(--radius-sm);
    color: var(--danger);
    font-size: 11px;
  }
  .fields {
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .label {
    font-size: 11px;
    color: var(--muted);
  }
  input {
    width: 100%;
    background: var(--bg-2);
    color: var(--fg);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: 4px 6px;
    font-family: var(--mono);
    font-size: 11px;
  }
  input:hover:not(:disabled) {
    border-color: var(--border-hi);
  }
  input:focus {
    outline: none;
    border-color: var(--accent);
    box-shadow: var(--glow) color-mix(in srgb, var(--accent) 45%, transparent);
  }
  input:disabled {
    color: var(--muted);
    background: color-mix(in srgb, var(--bg-2) 60%, transparent);
    border-style: dashed;
    opacity: 0.75;
    cursor: not-allowed;
  }
  .field.ro .label {
    color: var(--faint);
  }
  .hint {
    font-size: 10px;
    color: var(--faint);
  }
  .err {
    font-size: 10px;
    color: var(--danger);
  }
  .ports {
    margin: 0;
    padding: 0;
    list-style: none;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .ports li {
    display: flex;
    gap: var(--sp-2);
    align-items: baseline;
  }
  .dir {
    font-size: 9px;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--faint);
    width: 22px;
    flex: none;
  }
  .port {
    font-family: var(--mono);
    font-size: 10.5px;
    color: var(--muted);
  }
  .muted {
    margin: 0;
    color: var(--muted);
    font-size: 11px;
  }
  @media (prefers-reduced-motion: reduce) {
    .inspector,
    .body {
      transition: none;
    }
  }
</style>
