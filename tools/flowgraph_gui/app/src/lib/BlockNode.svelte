<script lang="ts">
  import { Handle, Position, type NodeProps } from '@xyflow/svelte';
  import {
    NO_RUNNER_BADGE,
    NO_RUNNER_HINT,
    typeSignature,
    type BlockNode,
    type BlockNodeData
  } from './project';

  const props: NodeProps<BlockNode> = $props();
  const data = $derived(props.data);
  const block: BlockNodeData['block'] = $derived(data.block);
  const badge = $derived(block.in_graph ? NO_RUNNER_BADGE : 'unwired');
  const badgeHint = $derived(block.in_graph ? NO_RUNNER_HINT : 'declared but not in the runner list');
</script>

<div
  class="block"
  class:unwired={!data.hasRunner}
  class:readonly={!block.editable}
  title={block.read_only_reason ?? undefined}
>
  <header>
    <span class="name">{block.display_name ?? block.var}</span>
    {#if !data.hasRunner}
      <span class="badge unwired-badge" title={badgeHint}>{badge}</span>
    {/if}
    {#if !block.editable}
      <span class="badge" title={block.read_only_reason ?? 'read-only'}>read-only</span>
    {/if}
  </header>
  <div class="type">{typeSignature(block)}</div>
  <div class="var">{block.var}</div>

  <div class="ports">
    {#each data.inputs as slot (slot.id)}
      <div class="port" class:grow={slot.grow}>
        <Handle
          type="target"
          position={Position.Left}
          id={slot.id}
          isConnectable={props.isConnectable}
        />
        <span class="port-label">{slot.label}</span>
      </div>
    {/each}
  </div>

  {#if data.hasOutput}
    <Handle type="source" position={Position.Right} id="out" isConnectable={props.isConnectable} />
  {/if}
</div>

<style>
  .block {
    width: 100%;
    height: 100%;
    box-sizing: border-box;
    background: var(--bg-1);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: var(--sp-2) var(--sp-3);
    color: var(--fg);
    font-size: 12px;
    line-height: 1.4;
  }
  header {
    display: flex;
    align-items: center;
    gap: var(--sp-1);
    height: 17px;
  }
  .name {
    font-weight: 600;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .badge {
    margin-left: auto;
    flex: none;
    font-size: 9px;
    letter-spacing: 0.04em;
    padding: 0 var(--sp-1);
    border: 1px solid var(--faint);
    border-radius: var(--radius-xs);
    background: transparent;
    color: var(--muted);
    font-weight: 700;
    cursor: help;
  }
  .unwired-badge {
    border-color: var(--muted);
    background: var(--muted);
    color: var(--bg-0);
  }
  .badge + .badge {
    margin-left: 0;
  }
  .type,
  .var {
    font-family: var(--mono);
    font-size: 11px;
    height: 15px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .type {
    color: var(--fg);
  }
  .var {
    color: var(--muted);
  }
  .ports {
    margin: var(--sp-1) calc(-1 * var(--sp-3)) 0;
  }
  .port {
    position: relative;
    height: 18px;
    padding-left: var(--sp-3);
    display: flex;
    align-items: center;
  }
  .port-label {
    padding-left: var(--sp-1);
    font-size: 11px;
    color: var(--muted);
    font-family: var(--mono);
  }
  .port.grow .port-label {
    color: var(--faint);
    font-style: italic;
  }
  .port.grow :global(.svelte-flow__handle) {
    border-style: dashed;
    background: transparent;
  }
</style>
