<script lang="ts">
  type Props = { entries: [string, string][] };

  const { entries }: Props = $props();

  let open = $state(true);
</script>

{#if entries.length > 1}
  <div class="legend" data-testid="type-legend">
    <button
      class="head"
      data-testid="type-legend-toggle"
      aria-expanded={open}
      title={open ? 'Collapse the type legend' : 'Expand the type legend'}
      onclick={() => (open = !open)}
    >
      <span>types</span><span class="chevron">{open ? '▾' : '▸'}</span>
    </button>
    {#if open}
      <ul>
        {#each entries as [type, token] (type)}
          <li>
            <span class="swatch" style="background: var({token})"></span>
            <span class="name">{type}</span>
          </li>
        {/each}
      </ul>
    {/if}
  </div>
{/if}

<style>
  .legend {
    position: absolute;
    left: 52px;
    bottom: 15px;
    z-index: 5;
    min-width: 108px;
    max-width: 260px;
    padding: var(--sp-1);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
    animation: rise 150ms ease-out;
  }
  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--sp-3);
    width: 100%;
    padding: 2px var(--sp-2);
    background: transparent;
    border-color: transparent;
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  .head:hover:not(:disabled) {
    background: var(--bg-2);
    border-color: transparent;
  }
  .chevron {
    font-size: 11px;
  }
  ul {
    margin: 2px 0 0;
    padding: 0 var(--sp-2) 2px;
    list-style: none;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  li {
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .swatch {
    flex: none;
    width: 10px;
    height: 10px;
    border-radius: 2px;
  }
  .name {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--fg);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  @keyframes rise {
    from {
      opacity: 0;
      transform: translateY(4px);
    }
  }
  @media (prefers-reduced-motion: reduce) {
    .legend {
      animation: none;
    }
  }
</style>
