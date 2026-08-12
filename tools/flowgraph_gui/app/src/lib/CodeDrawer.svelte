<script lang="ts">
  import { untrack } from 'svelte';
  import { codeLines, type CodeMark } from './code';
  import type { Placed } from './diagnostics';
  import type { Span } from './schema';

  export type Tab = 'code' | 'diagnostics' | 'output';

  type Props = {
    open: boolean;
    source: string;
    path: string;
    revision: number;
    readOnly: number;
    hits: Span[];
    marks: CodeMark[];
    anchors: Span[];
    siteAnchor: Span | null;
    height: number;
    tab: Tab;
    diagnostics: Placed[];
    output: string[];
    busy: string | null;
    onpick: (offset: number) => void;
    ontoggle: () => void;
    onheight: (height: number) => void;
    ontab: (tab: Tab) => void;
    ondiagnostic: (entry: Placed) => void;
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
    siteAnchor,
    height,
    tab,
    diagnostics,
    output,
    busy,
    onpick,
    ontoggle,
    onheight,
    ontab,
    ondiagnostic
  }: Props = $props();

  const TABS: Tab[] = ['code', 'diagnostics', 'output'];

  const MIN_HEIGHT = 90;
  const TOP_GUTTER = 120;
  const REVEAL_FRACTION = 3;

  let body = $state<HTMLElement | null>(null);
  let stream = $state<HTMLElement | null>(null);
  let stuck = $state(true);
  let shown = $state(false);
  let dragged = $state<number | null>(null);

  const lines = $derived(codeLines(source, hits, marks, anchors));
  const basename = $derived(path.split('/').pop() ?? path);
  const anchor = $derived(hits[0]?.start ?? siteAnchor?.start ?? null);
  const tall = $derived(dragged ?? height);
  const failing = $derived(diagnostics.filter((entry) => entry.severity === 'error').length);
  const diagnosticText = $derived(
    diagnostics
      .flatMap((entry) => [`${place(entry)}: ${entry.message}`, ...entry.notes])
      .join('\n')
  );

  $effect(() => {
    const wanted = open;
    const frame = requestAnimationFrame(() => (shown = wanted));
    return () => cancelAnimationFrame(frame);
  });

  $effect(() => {
    const at = anchor;
    const visible = shown && tab === 'code';
    if (at === null || !visible) return;
    untrack(() => reveal(at));
  });

  $effect(() => {
    const host = stream;
    if (output.length === 0 || !host || !stuck) return;
    host.scrollTop = host.scrollHeight;
  });

  function trackScroll(event: Event) {
    const host = event.currentTarget;
    if (!(host instanceof HTMLElement)) return;
    stuck = host.scrollTop + host.clientHeight >= host.scrollHeight - 4;
  }

  function place(entry: Placed): string {
    return `${entry.file.split('/').pop() ?? entry.file}:${entry.line}`;
  }

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

  async function copyDiagnostics() {
    if (diagnosticText.length === 0) return;
    try {
      await navigator.clipboard.writeText(diagnosticText);
    } catch {
      // Clipboard access can be unavailable in a browser-only session.
    }
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
    <div class="tabs" data-testid="drawer-tabs">
      {#each TABS as name (name)}
        <button
          class="tab"
          class:on={tab === name}
          data-testid="tab-{name}"
          aria-pressed={tab === name}
          onclick={() => ontab(name)}
        >
          {name}{#if name === 'diagnostics' && diagnostics.length > 0}
            <span class="count" class:danger={failing > 0}>{diagnostics.length}</span>
          {/if}
        </button>
      {/each}
    </div>
    {#if readOnly > 0}
      <span class="chip locked" data-testid="drawer-readonly">
        {readOnly} read-only
      </span>
    {/if}
    {#if busy}
      <span class="chip" data-testid="drawer-busy">{busy}…</span>
    {/if}
    <span class="grow"></span>
    <button
      class="close"
      data-testid="drawer-close"
      aria-label="Collapse code drawer"
      title="Collapse code drawer  Ctrl+`"
      onclick={ontoggle}>▾</button
    >
  </header>
  <!-- svelte-ignore a11y_click_events_have_key_events -->
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div
    class="body"
    class:away={tab !== 'code'}
    bind:this={body}
    data-testid="drawer-body"
    onclick={pickFrom}
  >
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

  {#if tab === 'diagnostics'}
    <div class="panel" data-testid="diagnostics-list">
      {#if diagnostics.length > 0}
        <button class="copy" data-testid="copy-diagnostics" onclick={() => void copyDiagnostics()}>
          Copy diagnostics
        </button>
      {/if}
      {#each diagnostics as entry (entry.id)}
        <button data-diagnostic={entry.id} onclick={() => ondiagnostic(entry)}>
          <span class="dot {entry.severity}"></span>
          <span class="what">{entry.message}</span>
          {#if entry.block}<span class="owner" data-diagnostic-block>{entry.block}</span>{/if}
          <span class="key">{place(entry)}</span>
        </button>
        {#each entry.notes as note (note)}
          <span class="note">{note}</span>
        {/each}
      {:else}
        <span class="empty" data-testid="diagnostics-empty">
          {busy === 'check' ? 'checking…' : 'nothing from the compiler — press F7 to check'}
        </span>
      {/each}
    </div>
  {/if}

  {#if tab === 'output'}
    <div
      class="panel stream"
      bind:this={stream}
      data-testid="output-body"
      onscroll={trackScroll}
    >
      {#each output as line, index (index)}
        <div class="line">{line}</div>
      {:else}
        <span class="empty">no output yet</span>
      {/each}
    </div>
  {/if}
</section>

<style>
  .drawer {
    position: absolute;
    left: calc(var(--rail-left) + 2 * var(--sp-3));
    right: calc(var(--rail-right) + 2 * var(--sp-3));
    bottom: var(--sp-3);
    z-index: 7;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
    transition:
      height 150ms ease,
      left 150ms ease,
      right 150ms ease;
  }
  .drawer.collapsed {
    border-width: 0;
    box-shadow: none;
  }
  .grip {
    flex: none;
    height: var(--sp-1);
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
  .tabs {
    display: flex;
    gap: var(--sp-1);
  }
  .tab {
    flex: none;
    width: auto;
    padding: 0 var(--sp-2);
    background: transparent;
    border-color: transparent;
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  .tab.on {
    background: var(--bg-2);
    border-color: var(--border-hi);
    color: var(--fg);
  }
  .count {
    margin-left: var(--sp-1);
    font-family: var(--mono);
    color: var(--fg);
  }
  .count.danger {
    color: var(--danger-fg);
  }
  .file {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--fg);
  }
  .chip {
    padding: 0 var(--sp-2);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    background: var(--bg-2);
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .chip.locked {
    border-color: var(--faint);
  }
  .grow {
    flex: 1;
  }
  .close {
    flex: none;
    width: 26px;
    padding: 0;
    font-size: 14px;
    line-height: 1.1;
    color: var(--muted);
  }
  .body {
    flex: 1;
    min-height: 0;
    overflow: auto;
    padding-bottom: var(--sp-2);
    font-family: var(--mono);
    font-size: 12px;
    line-height: 1.5;
  }
  .collapsed .body {
    visibility: hidden;
  }
  .body.away {
    display: none;
  }
  .panel {
    flex: 1;
    min-height: 0;
    overflow: auto;
    padding: 0 var(--sp-2) var(--sp-2);
    display: flex;
    flex-direction: column;
    gap: var(--sp-0);
    font-size: 12px;
  }
  .panel button {
    display: flex;
    align-items: baseline;
    gap: var(--sp-2);
    padding: var(--sp-0) var(--sp-2);
    background: transparent;
    border-color: transparent;
    text-align: left;
  }
  .panel button:hover {
    background: var(--bg-2);
    border-color: transparent;
  }
  .panel .copy {
    align-self: flex-end;
    width: auto;
    margin-bottom: var(--sp-1);
    border-color: var(--border);
    color: var(--muted);
    font-size: 11px;
  }
  .dot {
    flex: none;
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--danger);
  }
  .dot.warning {
    background: var(--warn-border);
  }
  .what {
    flex: 0 1 auto;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    color: var(--fg);
  }
  .owner {
    flex: none;
    padding: 0 var(--sp-1);
    border: 1px solid var(--border-hi);
    border-radius: var(--radius-xs);
    font-family: var(--mono);
    font-size: 11px;
    color: var(--fg);
  }
  .key {
    margin-left: auto;
    flex: none;
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .note {
    padding: 0 var(--sp-2) 0 calc(2 * var(--sp-4));
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .empty {
    padding: var(--sp-1) var(--sp-2);
    font-size: 11px;
    color: var(--muted);
  }
  .stream {
    font-family: var(--mono);
    font-size: 12px;
    line-height: 1.5;
  }
  .line {
    white-space: pre-wrap;
    word-break: break-word;
    color: var(--fg);
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
    border-radius: var(--radius-xs);
  }
  .ro {
    text-decoration: underline wavy var(--faint);
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
