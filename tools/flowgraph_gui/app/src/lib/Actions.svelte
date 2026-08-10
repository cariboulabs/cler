<script lang="ts">
  import { useSvelteFlow } from '@xyflow/svelte';

  import type { Problem } from './project';

  export type EdgeInfo = {
    title: string;
    detail: string;
    editable: boolean;
    reason: string | null;
  };

  type Props = {
    canUndo: boolean;
    canRedo: boolean;
    canOpenEditor: boolean;
    canEdit: boolean;
    selectedNode: string | null;
    selectedEdge: string | null;
    problems: Problem[];
    edgeAt: (id: string) => EdgeInfo | null;
    onundo: () => void;
    onredo: () => void;
    onopen: () => void;
    ontoggleleft: () => void;
    ontoggleright: () => void;
    ontoggledrawer: () => void;
    onviewsource: (block: string) => void;
    oncopydeclaration: (block: string) => void;
    onopeneditor: (block: string) => void;
    onremove: (block: string) => void;
    ondeleteblock: (block: string) => void;
    ondisconnect: (edge: string) => void;
    onaddhere: (clientX: number, clientY: number) => void;
    onproblem: (problem: Problem) => void;
  };

  type Action = {
    id: string;
    label: string;
    shortcut: string;
    enabled: boolean;
    hint?: string;
    run: () => void;
  };

  type Menu = { x: number; y: number; node: string | null; edge: string | null };

  const {
    canUndo,
    canRedo,
    canOpenEditor,
    canEdit,
    selectedNode,
    selectedEdge,
    problems,
    edgeAt,
    onundo,
    onredo,
    onopen,
    ontoggleleft,
    ontoggleright,
    ontoggledrawer,
    onviewsource,
    oncopydeclaration,
    onopeneditor,
    onremove,
    ondeleteblock,
    ondisconnect,
    onaddhere,
    onproblem
  }: Props = $props();

  const flow = useSvelteFlow();

  const ZOOM_MS = 150;
  const FIT_MS = 200;
  const SAVED_MS = 1500;
  const MENU_WIDTH = 200;
  const MENU_HEIGHT = 160;
  const MENU_IDS = ['undo', 'redo', 'fit'];
  const CTRL_KEYS: Record<string, string> = {
    y: 'redo',
    o: 'open',
    '=': 'zoom-in',
    '+': 'zoom-in',
    '-': 'zoom-out',
    _: 'zoom-out',
    '0': 'fit',
    '`': 'drawer'
  };
  const ICONS: Record<string, string[]> = {
    open: ['M2 12.6V4.4a.9.9 0 0 1 .9-.9h3.1l1.3 1.6h5.8a.9.9 0 0 1 .9.9v6.6a.9.9 0 0 1-.9.9H2.9a.9.9 0 0 1-.9-.9Z'],
    undo: ['M2.8 6.8h7.4a3.4 3.4 0 1 1 0 6.8H6.4', 'M5.4 4.2 2.8 6.8l2.6 2.6'],
    redo: ['M13.2 6.8H5.8a3.4 3.4 0 1 0 0 6.8H9.6', 'M10.6 4.2 13.2 6.8l-2.6 2.6'],
    'zoom-out': ['M7 2.5a4.5 4.5 0 1 0 0 9 4.5 4.5 0 0 0 0-9Z', 'M10.4 10.4 14 14', 'M4.9 7h4.2'],
    'zoom-in': [
      'M7 2.5a4.5 4.5 0 1 0 0 9 4.5 4.5 0 0 0 0-9Z',
      'M10.4 10.4 14 14',
      'M4.9 7h4.2',
      'M7 4.9v4.2'
    ],
    fit: [
      'M2 6V3.5A1.5 1.5 0 0 1 3.5 2H6',
      'M10 2h2.5A1.5 1.5 0 0 1 14 3.5V6',
      'M14 10v2.5a1.5 1.5 0 0 1-1.5 1.5H10',
      'M6 14H3.5A1.5 1.5 0 0 1 2 12.5V10'
    ],
    drawer: [
      'M2.4 2.6h11.2a.9.9 0 0 1 .9.9v9a.9.9 0 0 1-.9.9H2.4a.9.9 0 0 1-.9-.9v-9a.9.9 0 0 1 .9-.9Z',
      'M1.5 9.4h13',
      'M4.4 5.1 6.2 6.9 4.4 8.7',
      'M8 8.7h3.4'
    ]
  };

  let menu = $state<Menu | null>(null);
  let saved = $state(false);
  let savedTimer = 0;
  let problemsOpen = $state(false);

  const actions = $derived<Action[]>([
    { id: 'open', label: 'Open file', shortcut: 'Ctrl+O', enabled: true, run: onopen },
    { id: 'undo', label: 'Undo', shortcut: 'Ctrl+Z', enabled: canUndo, run: onundo },
    { id: 'redo', label: 'Redo', shortcut: 'Ctrl+Shift+Z', enabled: canRedo, run: onredo },
    {
      id: 'zoom-out',
      label: 'Zoom out',
      shortcut: 'Ctrl+-',
      enabled: true,
      run: () => void flow.zoomOut({ duration: ZOOM_MS })
    },
    {
      id: 'zoom-in',
      label: 'Zoom in',
      shortcut: 'Ctrl+=',
      enabled: true,
      run: () => void flow.zoomIn({ duration: ZOOM_MS })
    },
    {
      id: 'fit',
      label: 'Fit view',
      shortcut: 'Ctrl+0',
      enabled: true,
      run: () => void flow.fitView({ padding: 0.15, duration: FIT_MS })
    },
    {
      id: 'drawer',
      label: 'Toggle code',
      shortcut: 'Ctrl+`',
      enabled: true,
      run: ontoggledrawer
    }
  ]);

  function paneEntries(at: Menu): Action[] {
    return [
      {
        id: 'add-here',
        label: 'Add block here…',
        shortcut: '',
        enabled: canEdit,
        hint: canEdit ? undefined : EXAMPLE_NOTE,
        run: () => onaddhere(at.x, at.y)
      },
      ...actions.filter((action) => MENU_IDS.includes(action.id))
    ];
  }

  const EXAMPLE_NOTE = 'example mode — editing needs the desktop shell';

  function nodeEntries(block: string): Action[] {
    return [
      {
        id: 'view-source',
        label: 'View source',
        shortcut: 'Ctrl+`',
        enabled: true,
        run: () => onviewsource(block)
      },
      {
        id: 'copy-declaration',
        label: 'Copy declaration',
        shortcut: '',
        enabled: true,
        run: () => oncopydeclaration(block)
      },
      {
        id: 'open-editor',
        label: 'Open in editor',
        shortcut: '',
        enabled: canOpenEditor,
        hint: canOpenEditor ? undefined : 'opening an editor needs the desktop shell',
        run: () => onopeneditor(block)
      },
      {
        id: 'remove',
        label: 'Remove from graph',
        shortcut: 'Del',
        enabled: canEdit,
        hint: canEdit ? 'splices the runner out, the declaration stays' : EXAMPLE_NOTE,
        run: () => onremove(block)
      },
      {
        id: 'delete-block',
        label: 'Delete block…',
        shortcut: '',
        enabled: canEdit,
        hint: canEdit ? 'removes the declaration too' : EXAMPLE_NOTE,
        run: () => ondeleteblock(block)
      }
    ];
  }

  function edgeEntries(edge: string): Action[] {
    const found = edgeAt(edge);
    const editable = canEdit && found?.editable !== false;
    return [
      {
        id: 'disconnect',
        label: 'Disconnect',
        shortcut: 'Del',
        enabled: editable,
        hint: editable ? undefined : (found?.reason ?? EXAMPLE_NOTE),
        run: () => ondisconnect(edge)
      }
    ];
  }

  const entries = $derived.by(() => {
    if (!menu) return [];
    if (menu.node) return nodeEntries(menu.node);
    if (menu.edge) return edgeEntries(menu.edge);
    return paneEntries(menu);
  });

  const menuKind = $derived(menu?.node ? 'block' : menu?.edge ? 'edge' : 'pane');
  const menuInfo = $derived.by(() => {
    if (!menu?.edge) return null;
    const found = edgeAt(menu.edge);
    if (!found) return null;
    return `${found.title} — ${found.detail}`;
  });

  function byId(id: string): Action | undefined {
    return actions.find((action) => action.id === id);
  }

  function act(action: Action | undefined) {
    menu = null;
    if (action?.enabled) action.run();
  }

  function flashSaved() {
    saved = true;
    clearTimeout(savedTimer);
    savedTimer = setTimeout(() => (saved = false), SAVED_MS);
  }

  function isTyping(target: EventTarget | null): boolean {
    if (!(target instanceof HTMLElement)) return false;
    return target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName);
  }

  function shortcutId(key: string, shift: boolean): string | undefined {
    if (key === 'z') return shift ? 'redo' : 'undo';
    return CTRL_KEYS[key];
  }

  function deleteSelection() {
    if (!canEdit) return;
    if (selectedEdge) ondisconnect(selectedEdge);
    else if (selectedNode) onremove(selectedNode);
  }

  function onKeydown(event: KeyboardEvent) {
    if (event.key === 'Escape') {
      menu = null;
      problemsOpen = false;
      return;
    }
    const ctrl = event.ctrlKey || event.metaKey;
    const key = event.key.toLowerCase();
    if (ctrl && key === 's') {
      flashSaved();
      event.preventDefault();
      return;
    }
    if (isTyping(event.target)) return;
    if (ctrl) {
      const id = shortcutId(key, event.shiftKey);
      if (!id) return;
      act(byId(id));
      event.preventDefault();
      return;
    }
    if (event.altKey) return;
    if (key === 'delete' || key === 'backspace') deleteSelection();
    else if (key === '[') ontoggleleft();
    else if (key === ']') ontoggleright();
    else return;
    event.preventDefault();
  }

  function onContextMenu(event: MouseEvent) {
    menu = null;
    const target = event.target;
    if (!(target instanceof Element)) return;
    const node = target.closest('.svelte-flow__node')?.getAttribute('data-id') ?? null;
    const edge = node
      ? null
      : (target.closest('.svelte-flow__edge')?.getAttribute('data-id') ?? null);
    if (!node && !edge && !target.closest('.svelte-flow__pane')) return;
    event.preventDefault();
    problemsOpen = false;
    menu = {
      x: Math.min(event.clientX, window.innerWidth - MENU_WIDTH),
      y: Math.min(event.clientY, window.innerHeight - MENU_HEIGHT),
      node,
      edge
    };
  }

  function pickProblem(problem: Problem) {
    problemsOpen = false;
    onproblem(problem);
  }
</script>

<svelte:window
  onkeydown={onKeydown}
  oncontextmenu={onContextMenu}
  onclick={() => {
    menu = null;
    problemsOpen = false;
  }}
/>

<div class="bar" data-testid="top-bar">
  <img src="/brand/cler_mark.png" alt="cler" width="22" height="22" />
  <span class="wordmark">cler</span>
  <span class="tagline">flowgraph editor</span>
  <span class="grow"></span>
  <button
    class="problems"
    class:danger={problems.length > 0}
    data-testid="problems"
    data-count={problems.length}
    aria-expanded={problemsOpen}
    title="{problems.length} type conflicts and unresolved elements in this site"
    onclick={(event) => {
      event.stopPropagation();
      menu = null;
      problemsOpen = !problemsOpen;
    }}
  >
    {problems.length} problem{problems.length === 1 ? '' : 's'}
  </button>
  {#each actions as action (action.id)}
    {#if action.id === 'zoom-out' || action.id === 'drawer'}
      <span class="sep"></span>
    {/if}
    <button
      class="icon"
      data-testid={action.id}
      aria-label={action.label}
      title="{action.label} ({action.shortcut})"
      disabled={!action.enabled}
      onclick={() => act(action)}
    >
      <svg
        viewBox="0 0 16 16"
        width="16"
        height="16"
        fill="none"
        stroke="currentColor"
        stroke-width="1.4"
        stroke-linecap="round"
        stroke-linejoin="round"
        aria-hidden="true"
      >
        {#each ICONS[action.id] ?? [] as d (d)}
          <path {d} />
        {/each}
      </svg>
    </button>
  {/each}
</div>

{#if saved}
  <div class="toast" data-testid="saved-toast">saved automatically</div>
{/if}

{#if problemsOpen}
  <div class="drop" data-testid="problems-list">
    {#each problems as problem (problem.id)}
      <button data-problem={problem.id} onclick={() => pickProblem(problem)}>
        <span class="kind {problem.kind}">{problem.kind}</span>
        <span class="what">{problem.title}</span>
        <span class="key">{problem.detail}</span>
      </button>
    {:else}
      <span class="empty">no type conflicts, nothing unresolved</span>
    {/each}
  </div>
{/if}

{#if menu}
  <div
    class="menu"
    data-testid="context-menu"
    data-menu={menuKind}
    style="left: {menu.x}px; top: {menu.y}px"
  >
    {#if menuInfo}
      <span class="info" data-testid="menu-edge-info">{menuInfo}</span>
    {/if}
    {#each entries as entry (entry.id)}
      <button
        data-testid="menu-{entry.id}"
        title={entry.hint}
        disabled={!entry.enabled}
        onclick={() => act(entry)}
      >
        <span>{entry.label}</span><span class="key">{entry.shortcut}</span>
      </button>
    {/each}
  </div>
{/if}

<style>
  .bar {
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    z-index: 6;
    height: 40px;
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    padding: 0 var(--sp-2) 0 var(--sp-3);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border-bottom: 1px solid var(--border);
  }
  .bar img {
    border-radius: var(--radius-sm);
    flex: none;
  }
  .wordmark {
    font-size: 14px;
    font-weight: 600;
    letter-spacing: 0.02em;
    color: var(--fg);
  }
  .tagline {
    font-size: 11px;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--muted);
  }
  .grow {
    flex: 1;
  }
  .sep {
    width: 1px;
    height: 18px;
    background: var(--border);
    margin: 0 var(--sp-1);
  }
  .icon {
    flex: none;
    width: 28px;
    height: 28px;
    padding: 0;
    display: grid;
    place-items: center;
    background: transparent;
    border-color: transparent;
    color: var(--fg);
  }
  .icon:disabled {
    color: var(--muted);
  }
  .icon:hover:not(:disabled) {
    background: var(--bg-2);
    border-color: var(--border-hi);
    color: var(--fg);
  }
  .menu {
    position: fixed;
    z-index: 20;
    min-width: 180px;
    padding: var(--sp-1);
    display: flex;
    flex-direction: column;
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
  }
  .menu button {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--sp-4);
    padding: 4px var(--sp-2);
    background: transparent;
    border-color: transparent;
    text-align: left;
  }
  .menu button:hover:not(:disabled) {
    background: var(--bg-2);
    border-color: transparent;
  }
  .info {
    padding: 3px var(--sp-2) 5px;
    border-bottom: 1px solid var(--border);
    margin-bottom: var(--sp-1);
    font-family: var(--mono);
    font-size: 10.5px;
    color: var(--muted);
  }
  .key {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .problems {
    flex: none;
    padding: 2px 7px;
    background: var(--bg-2);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    font-size: 11px;
    color: var(--muted);
  }
  .problems.danger {
    border-color: var(--danger-border);
    background: var(--danger-bg);
    color: var(--danger-fg);
  }
  .drop {
    position: absolute;
    top: 44px;
    right: var(--sp-3);
    z-index: 20;
    min-width: 280px;
    max-width: 420px;
    max-height: 300px;
    overflow-y: auto;
    padding: var(--sp-1);
    display: flex;
    flex-direction: column;
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
  }
  .drop button {
    display: flex;
    align-items: baseline;
    gap: var(--sp-2);
    padding: 3px var(--sp-2);
    background: transparent;
    border-color: transparent;
    text-align: left;
  }
  .drop button:hover {
    background: var(--bg-2);
    border-color: transparent;
  }
  .kind {
    flex: none;
    padding: 0 4px;
    border-radius: 3px;
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.04em;
    background: var(--danger);
    color: var(--accent-fg);
  }
  .kind.unresolved {
    background: var(--warn-border);
    color: var(--bg-0);
  }
  .what {
    font-family: var(--mono);
    font-size: 11px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .drop .key {
    margin-left: auto;
    flex: none;
  }
  .empty {
    padding: 3px var(--sp-2);
    font-size: 11px;
    color: var(--muted);
  }
  .toast {
    position: absolute;
    top: 48px;
    left: 50%;
    z-index: 20;
    transform: translateX(-50%);
    padding: 5px var(--sp-3);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
    color: var(--muted);
    font-size: 11.5px;
    animation: rise 150ms ease-out;
  }
  @keyframes rise {
    from {
      opacity: 0;
      transform: translate(-50%, -4px);
    }
  }
  @media (prefers-reduced-motion: reduce) {
    .toast {
      animation: none;
    }
  }
</style>
