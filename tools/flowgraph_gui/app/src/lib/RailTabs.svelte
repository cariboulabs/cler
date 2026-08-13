<script lang="ts">
  export type RailTab = 'inspector' | 'library' | 'assistant';

  type Props = { tab: RailTab; ontab: (next: RailTab) => void };

  const { tab, ontab }: Props = $props();

  const TABS: [RailTab, string, string][] = [
    ['inspector', 'Inspector', 'Block parameters and ports  ]'],
    ['library', 'Library', 'Search and place blocks'],
    ['assistant', 'AI Agent', 'Ask about this flowgraph  Ctrl+J']
  ];
</script>

<div class="tabs" role="tablist" data-testid="rail-tabs">
  {#each TABS as [id, label, title] (id)}
    <button
      role="tab"
      data-testid="rail-tab-{id}"
      aria-selected={tab === id}
      class:on={tab === id}
      {title}
      onclick={() => ontab(id)}>{label}</button
    >
  {/each}
</div>

<style>
  .tabs {
    display: flex;
    gap: var(--sp-1);
    min-width: 0;
  }
  button {
    padding: var(--sp-0) var(--sp-2);
    background: transparent;
    border-color: transparent;
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    font-weight: 600;
    color: var(--muted);
    white-space: nowrap;
  }
  button.on {
    background: var(--bg-2);
    border-color: var(--border);
    color: var(--fg);
  }
</style>
