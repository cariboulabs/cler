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
  };

  const { site, siteIndex, selected, enabled, submit }: Props = $props();

  let drafts = $state<Record<string, string>>({});
  let errors = $state<Record<string, string>>({});

  const block = $derived(site?.blocks.find((candidate) => candidate.var === selected));
  const fields = $derived(block ? blockFields(siteIndex, block) : []);
  const config = $derived(site?.config ?? null);
  const configRows = $derived(config ? configFields(siteIndex, config) : []);
  const ports = $derived(site && block ? blockPorts(site, block.var) : null);

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

<aside class="inspector">
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
</aside>

<style>
  .inspector {
    background: var(--bg-1);
    border-left: 1px solid var(--border);
    padding: var(--sp-3);
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: var(--sp-4);
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
</style>
