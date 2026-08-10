<script lang="ts">
  import {
    categoryOf,
    ctorSignature,
    DRAG_TYPE,
    portsSummary,
    searchSpecs,
    type BlockSpec
  } from './palette';

  type Props = {
    specs: BlockSpec[];
    documentPath: string;
    enabled: boolean;
    open: boolean;
    ontoggle: () => void;
    onpick: (spec: BlockSpec) => void;
  };

  const { specs, documentPath, enabled, open, ontoggle, onpick }: Props = $props();

  let query = $state('');
  let expanded = $state<string | null>(null);

  const entries = $derived(searchSpecs(specs, query, documentPath));

  function startDrag(event: DragEvent, spec: BlockSpec) {
    if (!enabled || !event.dataTransfer) return;
    event.dataTransfer.setData(DRAG_TYPE, spec.name);
    event.dataTransfer.effectAllowed = 'copy';
  }
</script>

<section data-testid="palette">
  <h2>
    <button class="head" aria-expanded={open} data-testid="palette-toggle" onclick={ontoggle}>
      <span class="caret">{open ? '▾' : '▸'}</span>Blocks
      <span class="count">{specs.length}</span>
    </button>
  </h2>

  {#if open}
    <input
      type="search"
      placeholder="search blocks"
      data-testid="palette-search"
      bind:value={query}
    />
    {#if !enabled}
      <p class="muted" data-testid="palette-notice">
        example mode — dragging a block onto the canvas needs the desktop shell
      </p>
    {/if}
    <ul>
      {#each entries as spec (spec.origin + spec.name)}
        <li
          class="entry"
          class:locked={!enabled}
          data-block={spec.name}
          draggable={enabled}
          ondragstart={(event) => startDrag(event, spec)}
        >
          <button
            class="row"
            title={ctorSignature(spec)}
            onclick={() => (expanded = expanded === spec.name ? null : spec.name)}
            ondblclick={() => enabled && onpick(spec)}
          >
            <span class="name">{spec.name}</span>
            <span class="cat">{categoryOf(spec, documentPath)}</span>
            <span class="ports">{portsSummary(spec)}</span>
            {#if spec.may_block}
              <span class="chip" title="this block may block its worker">may_block</span>
            {/if}
          </button>
          {#if expanded === spec.name}
            <code class="sig" data-testid="palette-signature">{ctorSignature(spec)}</code>
          {/if}
        </li>
      {:else}
        <li class="muted">nothing matches “{query}”</li>
      {/each}
    </ul>
  {/if}
</section>

<style>
  h2 {
    margin: 0 0 var(--sp-2);
  }
  .head {
    display: flex;
    align-items: center;
    gap: 5px;
    width: 100%;
    padding: 0;
    background: transparent;
    border-color: transparent;
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  .caret {
    width: 10px;
  }
  .count {
    margin-left: auto;
    font-family: var(--mono);
    letter-spacing: 0;
  }
  input {
    width: 100%;
    background: var(--bg-2);
    color: var(--fg);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: 4px 6px;
    font-size: 11px;
  }
  input:focus {
    outline: none;
    border-color: var(--accent);
  }
  ul {
    margin: var(--sp-2) 0 0;
    padding: 0;
    list-style: none;
    max-height: 260px;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 1px;
  }
  .entry {
    border-left: 2px solid transparent;
  }
  .entry:not(.locked) {
    cursor: grab;
  }
  .entry:hover {
    border-left-color: var(--accent);
  }
  .row {
    display: flex;
    align-items: baseline;
    gap: 5px;
    width: 100%;
    padding: 2px 6px;
    background: transparent;
    border-color: transparent;
    text-align: left;
  }
  .row:hover {
    background: var(--bg-2);
    border-color: transparent;
  }
  .name {
    font-size: 11.5px;
    color: var(--fg);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .cat {
    font-size: 10px;
    color: var(--muted);
  }
  .ports {
    margin-left: auto;
    flex: none;
    font-family: var(--mono);
    font-size: 10px;
    color: var(--muted);
  }
  .chip {
    flex: none;
    padding: 0 4px;
    border-radius: 3px;
    background: var(--warn-bg);
    border: 1px solid var(--warn-border);
    font-size: 9px;
    color: var(--fg);
  }
  .sig {
    display: block;
    padding: 2px 6px 5px 8px;
    font-family: var(--mono);
    font-size: 10px;
    color: var(--muted);
    word-break: break-all;
  }
  .muted {
    margin: var(--sp-1) 0 0;
    color: var(--muted);
    font-size: 11px;
  }
</style>
