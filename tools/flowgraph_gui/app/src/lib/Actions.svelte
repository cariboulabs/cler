<script lang="ts">
  import { useSvelteFlow } from '@xyflow/svelte';

  import type { Problem } from './project';

  export type EdgeInfo = {
    title: string;
    detail: string;
    editable: boolean;
    reason: string | null;
  };

  export type AlertAction = { label: string; run: () => void };

  export type Alert = {
    text: string;
    at: number;
    tone: 'error' | 'note';
    action?: AlertAction;
  };

  export type FitPadding = {
    top: `${number}px`;
    right: `${number}px`;
    bottom: `${number}px`;
    left: `${number}px`;
  };

  export type Gate = { enabled: boolean; hint: string };

  export type Tasks = { check: Gate; build: Gate; run: Gate };

  type Props = {
    canUndo: boolean;
    canRedo: boolean;
    canOpenEditor: boolean;
    canEdit: boolean;
    demo: boolean;
    editNote: string;
    alert: Alert | null;
    leftOpen: boolean;
    rightOpen: boolean;
    fitPadding: FitPadding;
    selectedNode: string | null;
    selectedEdge: string | null;
    problems: Problem[];
    compiled: Problem[];
    tasks: Tasks;
    running: boolean;
    edgeAt: (id: string) => EdgeInfo | null;
    oncheck: () => void;
    onbuild: () => void;
    onrun: () => void;
    onundo: () => void;
    onredo: () => void;
    onopen: () => void;
    ontoggleleft: () => void;
    ontoggleright: () => void;
    ontoggledrawer: () => void;
    ontoggleassistant: () => void;
    ontogglechrome: () => void;
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
    demo,
    editNote,
    alert,
    leftOpen,
    rightOpen,
    fitPadding,
    selectedNode,
    selectedEdge,
    problems,
    compiled,
    tasks,
    running,
    edgeAt,
    oncheck,
    onbuild,
    onrun,
    onundo,
    onredo,
    onopen,
    ontoggleleft,
    ontoggleright,
    ontoggledrawer,
    ontoggleassistant,
    ontogglechrome,
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
  const RAIL_MS = 200;
  const SAVED_MS = 1500;
  const ALERT_MS = 4000;
  const MENU_WIDTH = 200;
  const MENU_HEIGHT = 260;
  const MENU_IDS = ['check', 'build', 'run', 'undo', 'redo', 'fit'];
  const TOOLBAR_IDS = [
    'check',
    'build',
    'run',
    'open',
    'undo',
    'redo',
    'zoom-out',
    'zoom-in',
    'fit'
  ];
  const CTRL_KEYS: Record<string, string> = {
    y: 'redo',
    o: 'open',
    b: 'build',
    r: 'run',
    '=': 'zoom-in',
    '+': 'zoom-in',
    '-': 'zoom-out',
    _: 'zoom-out',
    '0': 'fit',
    '`': 'drawer',
    j: 'assistant',
    '\\': 'chrome'
  };
  const ICONS: Record<string, string[]> = {
    check: ['M2.5 8.6 6 12.1 13.5 3.9'],
    build: ['M8 1.8 14 5v6l-6 3.2L2 11V5Z', 'M2 5l6 3.2L14 5', 'M8 8.2v6.4'],
    run: ['M5.5 3.4 12.5 8l-7 4.6Z'],
    stop: ['M4.6 4.6h6.8v6.8H4.6Z'],
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
    ]
  };

  let menu = $state<Menu | null>(null);
  let toast = $state<{ text: string; danger: boolean; action: AlertAction | null } | null>(null);
  let toastTimer: ReturnType<typeof setTimeout> | undefined;
  let problemsOpen = $state(false);

  const listed = $derived<Problem[]>([...compiled, ...problems]);
  const failing = $derived(listed.some((problem) => problem.severity === 'error'));

  let rails: string | null = null;

  $effect(() => {
    if (alert) flash(alert.text, alert.tone === 'error', ALERT_MS, alert.action ?? null);
  });

  $effect(() => {
    const shape = `${leftOpen}/${rightOpen}`;
    if (shape === rails) return;
    const first = rails === null;
    rails = shape;
    if (first) return;
    const timer = setTimeout(
      () => void flow.fitView({ padding: fitPadding, duration: FIT_MS }),
      RAIL_MS
    );
    return () => clearTimeout(timer);
  });

  const actions = $derived<Action[]>([
    {
      id: 'check',
      label: 'Check',
      shortcut: 'F7',
      enabled: tasks.check.enabled,
      hint: tasks.check.hint,
      run: oncheck
    },
    {
      id: 'build',
      label: 'Build',
      shortcut: 'Ctrl+B',
      enabled: tasks.build.enabled,
      hint: tasks.build.hint,
      run: onbuild
    },
    {
      id: 'run',
      label: running ? 'Stop' : 'Run',
      shortcut: 'Ctrl+R',
      enabled: tasks.run.enabled,
      hint: tasks.run.hint,
      run: onrun
    },
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
      run: () => void flow.fitView({ padding: fitPadding, duration: FIT_MS })
    },
    {
      id: 'drawer',
      label: 'Toggle code',
      shortcut: 'Ctrl+`',
      enabled: true,
      run: ontoggledrawer
    },
    {
      id: 'assistant',
      label: 'Toggle assistant',
      shortcut: 'Ctrl+J',
      enabled: true,
      hint: 'ask about this flowgraph (Ctrl+J)',
      run: ontoggleassistant
    },
    {
      id: 'chrome',
      label: leftOpen || rightOpen ? 'Hide chrome' : 'Show chrome',
      shortcut: 'Ctrl+\\',
      enabled: true,
      run: ontogglechrome
    }
  ]);
  const toolbarActions = $derived(actions.filter((action) => TOOLBAR_IDS.includes(action.id)));

  function paneEntries(at: Menu): Action[] {
    return [
      {
        id: 'add-here',
        label: 'Add block here…',
        shortcut: '',
        enabled: canEdit,
        hint: canEdit ? undefined : editNote,
        run: () => onaddhere(at.x, at.y)
      },
      ...actions.filter((action) => MENU_IDS.includes(action.id))
    ];
  }

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
        hint: canOpenEditor ? undefined : editNote,
        run: () => onopeneditor(block)
      },
      {
        id: 'remove',
        label: 'Remove from graph',
        shortcut: 'Del',
        enabled: canEdit,
        hint: canEdit ? 'splices the runner out, the declaration stays' : editNote,
        run: () => onremove(block)
      },
      {
        id: 'delete-block',
        label: 'Delete block…',
        shortcut: '',
        enabled: canEdit,
        hint: canEdit ? 'removes the declaration too' : editNote,
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
        hint: editable ? undefined : (found?.reason ?? editNote),
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

  function flash(text: string, danger: boolean, life: number, action: AlertAction | null = null) {
    toast = { text, danger, action };
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => (toast = null), life);
  }

  function takeToastAction(action: AlertAction) {
    toast = null;
    clearTimeout(toastTimer);
    action.run();
  }

  function flashSaved() {
    if (!canEdit) {
      flash(editNote, false, ALERT_MS);
      return;
    }
    flash('saved automatically', false, SAVED_MS);
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
    if (ctrl && key === 'r') event.preventDefault();
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
    else if (key === 'f7') act(byId('check'));
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
    menu = { x: event.clientX, y: event.clientY, node, edge };
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
  {#if demo}
    <span class="demo" data-testid="demo-chip" title={editNote}>demo</span>
  {/if}
  <span class="tagline">flowgraph editor</span>
  <span class="grow"></span>
  <button
    class="problems"
    class:danger={failing}
    class:clear={listed.length === 0}
    data-testid="problems"
    data-count={listed.length}
    aria-expanded={problemsOpen}
    disabled={listed.length === 0}
    title={listed.length === 0
      ? 'no conflicts, nothing unresolved, every block runs'
      : `${listed.length} compiler diagnostics, conflicts, unresolved elements and runnerless blocks`}
    onclick={(event) => {
      event.stopPropagation();
      menu = null;
      problemsOpen = !problemsOpen;
    }}
  >
    {listed.length} problem{listed.length === 1 ? '' : 's'}
  </button>
  {#each toolbarActions as action (action.id)}
    {#if action.id === 'open' || action.id === 'zoom-out'}
      <span class="sep"></span>
    {/if}
    <button
      class="icon"
      class:live={action.id === 'run' && running}
      data-testid={action.id}
      aria-label={action.label}
      title={action.enabled
        ? `${action.label} (${action.shortcut})`
        : (action.hint ?? `${action.label} (${action.shortcut})`)}
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
        {#each ICONS[action.id === 'run' && running ? 'stop' : action.id] ?? [] as d (d)}
          <path {d} />
        {/each}
      </svg>
    </button>
  {/each}
</div>

{#if toast}
  <div
    class="toast"
    class:danger={toast.danger}
    role={toast.danger ? 'alert' : undefined}
    data-testid={toast.danger ? 'alert-toast' : 'note-toast'}
  >
    <span>{toast.text}</span>
    {#if toast.action}
      {@const action = toast.action}
      <button
        class="act"
        data-testid="toast-action"
        onclick={(event) => {
          event.stopPropagation();
          takeToastAction(action);
        }}>{action.label}</button
      >
    {/if}
    {#if toast.danger}
      <button
        class="dismiss"
        data-testid="alert-dismiss"
        aria-label="Dismiss"
        onclick={() => (toast = null)}>×</button
      >
    {/if}
  </div>
{/if}

{#if problemsOpen}
  <div class="drop" data-testid="problems-list">
    {#if compiled.length > 0}
      <span class="section" data-testid="section-compiler">compiler</span>
    {/if}
    {#each compiled as problem (problem.id)}
      <button data-problem={problem.id} onclick={() => pickProblem(problem)}>
        <span class="kind {problem.severity}">{problem.kind}</span>
        <span class="what">{problem.title}</span>
        <span class="key">{problem.detail}</span>
      </button>
    {/each}
    {#if compiled.length > 0 && problems.length > 0}
      <span class="section" data-testid="section-graph">graph</span>
    {/if}
    {#each problems as problem (problem.id)}
      <button data-problem={problem.id} onclick={() => pickProblem(problem)}>
        <span class="kind {problem.severity}">{problem.kind}</span>
        <span class="what">{problem.title}</span>
        <span class="key">{problem.detail}</span>
      </button>
    {/each}
    {#if listed.length === 0}
      <span class="empty">no type conflicts, nothing unresolved</span>
    {/if}
  </div>
{/if}

{#if menu}
  <div
    class="menu"
    data-testid="context-menu"
    data-menu={menuKind}
    style="left: {Math.min(menu.x, window.innerWidth - MENU_WIDTH)}px; top: {Math.min(
      menu.y,
      window.innerHeight - MENU_HEIGHT
    )}px"
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
    z-index: 9;
    height: var(--bar-h);
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
  .demo {
    flex: none;
    padding: 0 var(--sp-1);
    border: 1px solid var(--border-hi);
    border-radius: var(--radius-xs);
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--muted);
    cursor: help;
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
  .icon.live {
    position: relative;
    color: var(--danger-fg);
  }
  .icon.live::after {
    content: '';
    position: absolute;
    top: 3px;
    right: 3px;
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: var(--danger);
  }
  .section {
    padding: var(--sp-1) var(--sp-2) var(--sp-0);
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    color: var(--muted);
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
    animation: lift 150ms ease-out;
  }
  .menu button {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--sp-4);
    padding: var(--sp-1) var(--sp-2);
    background: transparent;
    border-color: transparent;
    text-align: left;
  }
  .menu button:hover:not(:disabled) {
    background: var(--bg-2);
    border-color: transparent;
  }
  .info {
    padding: var(--sp-0) var(--sp-2) var(--sp-1);
    border-bottom: 1px solid var(--border);
    margin-bottom: var(--sp-1);
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .key {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .problems {
    flex: none;
    padding: var(--sp-0) var(--sp-2);
    background: var(--bg-2);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    font-size: 11px;
    color: var(--muted);
  }
  .problems.clear:disabled {
    border-color: color-mix(in srgb, var(--ok) 55%, var(--border));
    background: color-mix(in srgb, var(--ok) 14%, var(--bg-2));
    color: var(--muted);
    cursor: default;
  }
  .problems.danger {
    border-color: var(--danger-border);
    background: var(--danger-bg);
    color: var(--danger-fg);
  }
  .drop {
    position: absolute;
    top: calc(var(--bar-h) + var(--sp-1));
    right: var(--sp-3);
    z-index: 20;
    animation: lift 150ms ease-out;
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
    padding: var(--sp-0) var(--sp-2);
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
    padding: 0 var(--sp-1);
    border-radius: var(--radius-xs);
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.04em;
    background: var(--danger);
    color: var(--accent-fg);
  }
  .kind.warning {
    background: var(--warn-border);
    color: var(--bg-0);
  }
  .what {
    flex: none;
    font-family: var(--mono);
    font-size: 11px;
    white-space: nowrap;
  }
  .drop .key {
    margin-left: auto;
    flex: 0 1 auto;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .empty {
    padding: var(--sp-0) var(--sp-2);
    font-size: 11px;
    color: var(--muted);
  }
  .toast {
    position: absolute;
    top: calc(var(--bar-h) + var(--sp-2));
    left: 50%;
    z-index: 20;
    transform: translateX(-50%);
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    max-width: min(560px, 70vw);
    padding: var(--sp-1) var(--sp-3);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
    color: var(--muted);
    font-size: 12px;
    animation: rise 150ms ease-out;
  }
  .toast.danger {
    border-color: var(--danger-border);
    background: var(--danger-bg);
    color: var(--danger-fg);
  }
  .act {
    flex: none;
    width: auto;
    padding: var(--sp-0) var(--sp-2);
    font-size: 11px;
  }
  .dismiss {
    flex: none;
    width: 18px;
    padding: 0;
    background: transparent;
    border-color: transparent;
    color: inherit;
    font-size: 14px;
    line-height: 1;
  }
  @keyframes rise {
    from {
      opacity: 0;
      transform: translate(-50%, -4px);
    }
  }
  @keyframes lift {
    from {
      opacity: 0;
      transform: translateY(-4px);
    }
  }
  @media (prefers-reduced-motion: reduce) {
    .toast,
    .menu,
    .drop {
      animation: none;
    }
  }
</style>
