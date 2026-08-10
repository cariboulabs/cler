<script lang="ts">
  import { untrack } from 'svelte';
  import { codeLines, type CodeMark } from './code';
  import type { Span } from './schema';

  type Props = {
    open: boolean;
    source: string;
    path: string;
    revision: number;
    readOnly: number;
    hits: Span[];
    marks: CodeMark[];
    anchors: Span[];
    height: number;
    onpick: (offset: number) => void;
    ontoggle: () => void;
    onheight: (height: number) => void;
  };

  const {
    open,
    source,
    path,
    revision,
    readOnly,
    hits,
    marks,
    anchors,
    height,
    onpick,
    ontoggle,
    onheight
  }: Props = $props();

  const MIN_HEIGHT = 90;
  const TOP_GUTTER = 120;
  const REVEAL_FRACTION = 3;

  let body = $state<HTMLElement | null>(null);
  let shown = $state(false);
  let dragged = $state<number | null>(null);

  const lines = $derived(codeLines(source, hits, marks, anchors));
  const basename = $derived(path.split('/').pop() ?? path);
  const anchor = $derived(hits[0]?.start ?? null);
  const tall = $derived(dragged ?? height);

  $effect(() => {
    const wanted = open;
    const frame = requestAnimationFrame(() => (shown = wanted));
    return () => cancelAnimationFrame(frame);
  });

  $effect(() => {
    const at = anchor;
    const visible = shown;
    if (at === null || !visible) return;
    untrack(() => reveal(at));
  });

  function ceiling(): number {
    return Math.max(MIN_HEIGHT, window.innerHeight - TOP_GUTTER);
  }

  function lineIndexOf(offset: number): number {
    let index = 0;
    while (index + 1 < lines.length && (lines[index + 1]?.start ?? 0) <= offset) index++;
    return index;
  }

  function rowOf(offset: number): HTMLElement | null {
    const row = body?.children[lineIndexOf(offset)];
    return row instanceof HTMLElement ? row : null;
  }

  function reveal(offset: number) {
    const host = body;
    const row = rowOf(offset);
    if (!host || !row) return;
    const above = row.offsetTop < host.scrollTop;
    const below = row.offsetTop + row.offsetHeight > host.scrollTop + host.clientHeight;
    if (above || below) host.scrollTop = row.offsetTop - host.clientHeight / REVEAL_FRACTION;
  }

  function startDrag(event: PointerEvent) {
    const grip = event.currentTarget;
    if (!(grip instanceof HTMLElement)) return;
    const originY = event.clientY;
    const originHeight = tall;
    grip.setPointerCapture(event.pointerId);
    dragged = originHeight;

    const move = (moved: PointerEvent) => {
      dragged = Math.min(ceiling(), Math.max(MIN_HEIGHT, originHeight + originY - moved.clientY));
    };
    const stop = () => {
      grip.removeEventListener('pointermove', move);
      grip.removeEventListener('pointerup', stop);
      grip.removeEventListener('pointercancel', stop);
      const settled = dragged;
      dragged = null;
      if (settled !== null) onheight(settled);
    };
    grip.addEventListener('pointermove', move);
    grip.addEventListener('pointerup', stop);
    grip.addEventListener('pointercancel', stop);
  }

  function pickFrom(event: MouseEvent) {
    const target = event.target;
    if (!(target instanceof Element)) return;
    const piece = target.closest('[data-at]');
    if (!(piece instanceof HTMLElement)) return;
    const at = Number(piece.dataset.at);
    if (Number.isFinite(at)) onpick(at);
  }
</script>

<section
  class="drawer"
  class:collapsed={!shown}
  data-testid="code-drawer"
  style="height: {shown ? tall : 0}px"
>
  <div
    class="grip"
    data-testid="drawer-grip"
    role="separator"
    aria-orientation="horizontal"
    aria-label="Resize code drawer"
    onpointerdown={startDrag}
  ></div>
  <header>
    <h2>Code</h2>
    <span class="file" title={path} data-testid="drawer-file">{basename}</span>
    <span class="chip" data-testid="drawer-revision">rev {revision}</span>
    <span class="chip" class:danger={readOnly > 0} data-testid="drawer-readonly">
      {readOnly} read-only
    </span>
    <span class="grow"></span>
    <button
      class="close"
      data-testid="drawer-close"
      aria-label="Collapse code drawer"
      title="Collapse code drawer  Ctrl+`"
      onclick={ontoggle}>×</button
    >
  </header>
  <!-- svelte-ignore a11y_click_events_have_key_events -->
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="body" bind:this={body} data-testid="drawer-body" onclick={pickFrom}>
    {#each lines as line (line.number)}
      <div class="row" data-line={line.number}>
        <span class="num">{line.number}</span><code
          >{#each line.pieces as piece, index (index)}<span
              data-at={piece.at}
              class={piece.kind}
              class:hit={piece.hit}
              class:ro={piece.reason !== null}
              title={piece.reason ?? undefined}>{piece.text}</span
            >{/each}</code
        >
      </div>
    {/each}
  </div>
</section>

<style>
  .drawer {
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
    z-index: 7;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    background: var(--bg-1);
    border-top: 1px solid var(--border);
    transition: height 150ms ease;
  }
  .grip {
    flex: none;
    height: 5px;
    cursor: ns-resize;
    background: transparent;
  }
  .grip:hover {
    background: var(--accent);
  }
  header {
    flex: none;
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    padding: 0 var(--sp-2) var(--sp-1) var(--sp-3);
  }
  h2 {
    margin: 0;
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  .file {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--fg);
  }
  .chip {
    padding: 1px 6px;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    background: var(--bg-2);
    font-family: var(--mono);
    font-size: 10.5px;
    color: var(--muted);
  }
  .chip.danger {
    border-color: var(--danger-border);
    background: var(--danger-bg);
    color: var(--danger-fg);
  }
  .grow {
    flex: 1;
  }
  .close {
    flex: none;
    width: 26px;
    padding: 0;
    font-size: 15px;
    line-height: 1.1;
    color: var(--muted);
  }
  .body {
    flex: 1;
    min-height: 0;
    overflow: auto;
    padding-bottom: var(--sp-2);
    font-family: var(--mono);
    font-size: 11.5px;
    line-height: 1.5;
  }
  .collapsed .body {
    visibility: hidden;
  }
  .row {
    display: flex;
    align-items: flex-start;
    white-space: pre;
  }
  .num {
    flex: none;
    width: 46px;
    padding-right: var(--sp-3);
    text-align: right;
    color: var(--faint);
    user-select: none;
  }
  code {
    font: inherit;
    white-space: pre;
  }
  .hit {
    background: color-mix(in srgb, var(--accent) 30%, transparent);
    border-radius: 2px;
  }
  .ro {
    text-decoration: underline wavy var(--danger-border);
    text-underline-offset: 3px;
  }
  .kw {
    color: var(--type-5);
  }
  .typ {
    color: var(--type-1);
  }
  .str {
    color: var(--type-2);
  }
  .lit {
    color: var(--type-3);
  }
  .cmt {
    color: var(--faint);
    font-style: italic;
  }
  .pre {
    color: var(--type-3);
  }
  @media (prefers-reduced-motion: reduce) {
    .drawer {
      transition: none;
    }
  }
</style>
