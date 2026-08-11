<script lang="ts">
  import RailTabs, { type RailTab } from './RailTabs.svelte';
  import {
    ctorSignature,
    DRAG_TYPE,
    libraryGroups,
    portsSummary,
    searchSpecs,
    type BlockSpec,
    type LibraryGroup
  } from './palette';

  type Props = {
    specs: BlockSpec[];
    documentPath: string;
    enabled: boolean;
    open: boolean;
    ontoggle: () => void;
    ontab: (next: RailTab) => void;
    onpick: (spec: BlockSpec) => void;
  };

  const { specs, documentPath, enabled, open, ontoggle, ontab, onpick }: Props = $props();

  let query = $state('');
  let closed = $state.raw(new Set<string>());

  const entries = $derived(searchSpecs(specs, query, documentPath));
  const groups = $derived(libraryGroups(entries, documentPath));

  function groupOpen(group: LibraryGroup): boolean {
    return query.trim().length > 0 || !closed.has(group.path);
  }

  function toggleGroup(path: string): void {
    const next = new Set(closed);
    if (next.has(path)) next.delete(path);
    else next.add(path);
    closed = next;
  }

  function startDrag(event: DragEvent, spec: BlockSpec) {
    if (!enabled || !event.dataTransfer) return;
    event.dataTransfer.setData(DRAG_TYPE, spec.name);
    event.dataTransfer.effectAllowed = 'copy';
  }
</script>

{#snippet block(spec: BlockSpec)}
  <li
    class="entry"
    class:locked={!enabled}
    data-block={spec.name}
    draggable={enabled}
    ondragstart={(event) => startDrag(event, spec)}
  >
    <button class="row" title={ctorSignature(spec)} ondblclick={() => enabled && onpick(spec)}>
      <span class="name">{spec.name}</span>
      {#if portsSummary(spec)}
        <span class="ports">{portsSummary(spec)}</span>
      {/if}
      {#if spec.may_block}
        <span class="chip" title="this block may block its worker">may_block</span>
      {/if}
    </button>
  </li>
{/snippet}

{#snippet folder(group: LibraryGroup)}
  <li class="folder" data-library-path={group.path}>
    <button
      class="folder-row"
      aria-expanded={groupOpen(group)}
      onclick={() => toggleGroup(group.path)}
    >
      <span class="caret">{groupOpen(group) ? '▾' : '▸'}</span>
      <span class="folder-name">{group.name}</span>
      <span class="folder-count">{group.count}</span>
    </button>
    {#if groupOpen(group)}
      <ul class="folder-body">
        {#each group.groups as child (child.path)}
          {@render folder(child)}
        {/each}
        {#each group.specs as spec (spec.origin + spec.name)}
          {@render block(spec)}
        {/each}
      </ul>
    {/if}
  </li>
{/snippet}

<aside class="library" class:collapsed={!open} data-testid="palette">
  <div class="head">
    <button
      class="toggle"
      data-testid="toggle-library"
      aria-expanded={open}
      aria-label={open ? 'Collapse library' : 'Expand library'}
      title={open ? 'Collapse library' : 'Expand library'}
      onclick={ontoggle}
    >
      {#if open}
        ›
      {:else}
        <svg viewBox="0 0 16 16" width="15" height="15" fill="none" aria-hidden="true">
          <path
            d="M2.5 3.5h4v4h-4zM9.5 3.5h4v4h-4zM2.5 10.5h4v2h-4zM9.5 10.5h4v2h-4z"
            stroke="currentColor"
            stroke-width="1.3"
          />
        </svg>
      {/if}
    </button>
    <RailTabs tab="library" {ontab} />
  </div>

  <div class="body">
    <div class="title">
      <h2>Blocks</h2>
      <span class="count">{specs.length}</span>
    </div>
    <input
      type="search"
      placeholder="search blocks"
      data-testid="palette-search"
      bind:value={query}
    />
    <ul class="tree">
      {#each groups as group (group.path)}
        {@render folder(group)}
      {:else}
        <li class="muted">nothing matches “{query}”</li>
      {/each}
    </ul>
  </div>
</aside>

<style>
  .library {
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
  .collapsed :global(.tabs) {
    display: none;
  }
  .body {
    flex: 1;
    min-height: 0;
    width: 320px;
    padding: 0 var(--sp-3) var(--sp-3);
    display: flex;
    flex-direction: column;
    transition:
      opacity 120ms ease,
      visibility 150ms;
  }
  .collapsed .body {
    opacity: 0;
    visibility: hidden;
  }
  .title {
    display: flex;
    align-items: baseline;
    margin-bottom: var(--sp-2);
  }
  h2 {
    margin: 0;
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
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
  .tree,
  .folder-body {
    list-style: none;
  }
  .tree {
    margin: var(--sp-2) 0 0;
    padding: 0;
    flex: 1;
    min-height: 0;
    overflow-y: auto;
  }
  .folder-body {
    margin: 0;
    padding: 0 0 0 var(--sp-2);
  }
  .folder-row {
    display: flex;
    align-items: center;
    width: 100%;
    padding: var(--sp-0) var(--sp-1);
    background: transparent;
    border-color: transparent;
    color: var(--muted);
    text-align: left;
  }
  .folder-row:hover {
    background: var(--bg-2);
    border-color: transparent;
  }
  .caret {
    flex: none;
    width: 12px;
  }
  .folder-name {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .folder-count {
    margin-left: auto;
    font-family: var(--mono);
    font-size: 10px;
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
