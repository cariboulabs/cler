<script lang="ts">
  import RailTabs, { type RailTab } from './RailTabs.svelte';
  import FieldList from './FieldList.svelte';
  import { blockFields, type Outcome } from './inspector';
  import type { BlockSpec } from './palette';
  import { blockPorts, typeSignature } from './project';
  import type { Command, Site } from './schema';

  type Props = {
    path: string;
    site: Site | undefined;
    siteIndex: number;
    selected: string | null;
    spec: BlockSpec | undefined;
    enabled: boolean;
    submit: (command: Command) => Promise<Outcome>;
    open: boolean;
    ontoggle: () => void;
    ontab: (next: RailTab) => void;
  };

  const { path, site, siteIndex, selected, spec, enabled, submit, open, ontoggle, ontab }: Props =
    $props();

  let fieldList = $state<ReturnType<typeof FieldList> | null>(null);

  const scope = $derived(`${path}::${siteIndex}::`);
  const block = $derived(site?.blocks.find((candidate) => candidate.var === selected));
  const fields = $derived(block ? blockFields(siteIndex, block, spec, site) : []);
  const ports = $derived(site && block ? blockPorts(site, block.var) : null);
  const toggleTitle = $derived(
    open
      ? 'Collapse inspector  ]'
      : block
        ? `${block.display_name ?? block.var} selected — expand inspector  ]`
        : 'Expand inspector  ]'
  );

  export function discardDrafts(): void {
    fieldList?.discardDrafts();
  }

  function readable(text: string): string {
    return text.replace(/_/g, ' ');
  }
</script>



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
    <RailTabs tab="inspector" {ontab} />
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
        <FieldList
          bind:this={fieldList}
          {scope}
          {fields}
          ownerReason={block.read_only_reason}
          {enabled}
          {submit}
        />
      </section>

      {#if ports}
        <section>
          <h2>Ports</h2>
          {#if ports.inputs.length === 0 && ports.outputs.length === 0}
            <p class="muted">no wired ports</p>
          {:else}
            <ul class="ports">
              {#each ports.inputs as port (port.label)}
                <li>
                  <span class="dir">in</span><span class="port">{port.label}</span>
                  {#if port.type}<span class="sample" data-sample-type={port.label}>{port.type}</span
                    >{/if}
                </li>
              {/each}
              {#each ports.outputs as port (port.label)}
                <li>
                  <span class="dir">out</span><span class="port">{port.label}</span>
                  {#if port.type}<span class="sample" data-sample-type={port.label}>{port.type}</span
                    >{/if}
                </li>
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

  </div>
</aside>

<style>
  .inspector {
    position: absolute;
    z-index: 8;
    top: calc(var(--bar-h) + var(--sp-3));
    right: var(--sp-3);
    bottom: var(--sp-3);
    width: var(--rail-right);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
    overflow: hidden;
    display: flex;
    flex-direction: column;
    transition: width 150ms ease;
  }
  .head {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    padding: var(--sp-3);
  }
  .collapsed .head {
    padding: var(--sp-2);
  }
  .toggle {
    flex: none;
    width: 26px;
    padding: var(--sp-0) 0;
    font-size: 14px;
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
  .collapsed :global(.tabs) {
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
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
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
    gap: 0 var(--sp-3);
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
    border: 1px solid var(--faint);
    background: var(--bg-2);
    border-radius: var(--radius-sm);
    color: var(--muted);
    font-size: 11px;
  }
  .ports {
    margin: 0;
    padding: 0;
    list-style: none;
    display: flex;
    flex-direction: column;
    gap: var(--sp-0);
  }
  .ports li {
    display: flex;
    gap: var(--sp-2);
    align-items: baseline;
  }
  .dir {
    font-size: 11px;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--muted);
    width: 22px;
    flex: none;
  }
  .port {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .sample {
    margin-left: auto;
    padding-left: var(--sp-2);
    font-family: var(--mono);
    font-size: 11px;
    color: var(--fg);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
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
