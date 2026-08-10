<script lang="ts">
  import { Handle, Position, type NodeProps } from '@xyflow/svelte';
  import { typeSignature, type BlockNode, type BlockNodeData } from './project';

  const { data }: NodeProps<BlockNode> = $props();
  const block: BlockNodeData['block'] = $derived(data.block);
</script>

<div
  class="block"
  class:unwired={!block.in_graph}
  class:readonly={!block.editable}
  title={block.read_only_reason ?? undefined}
>
  <header>
    <span class="name">{block.display_name ?? block.var}</span>
    {#if !block.in_graph}
      <span class="badge unwired-badge" title="declared but not in the runner list">unwired</span>
    {/if}
    {#if !block.editable}
      <span class="badge" title={block.read_only_reason ?? 'read-only'}>read-only</span>
    {/if}
  </header>
  <div class="type">{typeSignature(block)}</div>
  <div class="var">{block.var}</div>

  <div class="ports">
    {#each data.inputs as slot (slot.id)}
      <div class="port">
        <Handle type="target" position={Position.Left} id={slot.id} isConnectable={false} />
        <span class="port-label">{slot.label}</span>
      </div>
    {/each}
  </div>

  {#if data.hasOutput}
    <Handle type="source" position={Position.Right} id="out" isConnectable={false} />
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
    padding: 8px 10px;
    color: var(--fg);
    font-size: 12px;
    line-height: 1.4;
  }
  .block.unwired {
    border-style: dashed;
    border-color: var(--faint);
    background: repeating-linear-gradient(
      135deg,
      var(--bg-1),
      var(--bg-1) 8px,
      var(--bg-2) 8px,
      var(--bg-2) 16px
    );
    color: var(--muted);
  }
  .block.readonly {
    border-color: var(--danger-border);
    background: var(--danger-bg);
  }
  header {
    display: flex;
    align-items: center;
    gap: 5px;
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
    padding: 1px 5px;
    border-radius: 4px;
    background: var(--danger);
    color: var(--accent-fg);
    font-weight: 700;
    cursor: help;
  }
  .unwired-badge {
    background: var(--muted);
    color: var(--bg-0);
  }
  .badge + .badge {
    margin-left: 0;
  }
  .type,
  .var {
    font-family: var(--mono);
    font-size: 10.5px;
    height: 14px;
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
    margin: 4px -10px 0;
  }
  .port {
    position: relative;
    height: 18px;
    padding-left: 10px;
    display: flex;
    align-items: center;
  }
  .port-label {
    padding-left: 4px;
    font-size: 11px;
    color: var(--muted);
    font-family: var(--mono);
  }
</style>
