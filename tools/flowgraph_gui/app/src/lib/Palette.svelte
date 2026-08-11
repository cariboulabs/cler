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

  const entries = $derived(searchSpecs(specs, query, documentPath));
  const categories = $derived(
    new Set(entries.map((spec) => categoryOf(spec, documentPath)))
  );
  const showCategory = $derived(categories.size > 1);

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
    <ul>
      {#each entries as spec (spec.origin + spec.name)}
        <li
          class="entry"
          class:locked={!enabled}
          data-block={spec.name}
          draggable={enabled}
          ondragstart={(event) => startDrag(event, spec)}
        >
          <button class="row" title={ctorSignature(spec)} ondblclick={() => enabled && onpick(spec)}>
            <span class="name">{spec.name}</span>
            {#if showCategory}
              <span class="cat">{categoryOf(spec, documentPath)}</span>
            {/if}
            {#if portsSummary(spec)}
              <span class="ports">{portsSummary(spec)}</span>
            {/if}
            {#if spec.may_block}
              <span class="chip" title="this block may block its worker">may_block</span>
            {/if}
          </button>
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
    font-size: 11px;
    line-height: 1.45;
  }
  .head {
    display: flex;
    align-items: center;
    gap: var(--sp-1);
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
    padding: var(--sp-1) var(--sp-2);
    font-size: 11px;
  }
  input:focus {
    outline: none;
    border-color: var(--accent-hi);
  }
  ul {
    margin: var(--sp-2) 0 0;
    padding: 0;
    list-style: none;
    max-height: 260px;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 0;
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
    gap: var(--sp-1);
    width: 100%;
    padding: var(--sp-0) var(--sp-2);
    background: transparent;
    border-color: transparent;
    text-align: left;
  }
  .row:hover {
    background: var(--bg-2);
    border-color: transparent;
  }
  .name {
    flex: 1 1 auto;
    min-width: 0;
    font-size: 12px;
    color: var(--fg);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .cat {
    flex: none;
    font-size: 11px;
    color: var(--muted);
  }
  .ports {
    flex: none;
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .chip {
    flex: none;
    padding: 0 var(--sp-1);
    border-radius: var(--radius-xs);
    background: var(--warn-bg);
    border: 1px solid var(--warn-border);
    font-size: 9px;
    color: var(--fg);
  }
  .muted {
    margin: var(--sp-1) 0 0;
    color: var(--muted);
    font-size: 11px;
  }
</style>
