<script lang="ts">
  import { useSvelteFlow } from '@xyflow/svelte';

  type Props = {
    canUndo: boolean;
    canRedo: boolean;
    busy: boolean;
    onundo: () => void;
    onredo: () => void;
    onopen: () => void;
    ontoggleleft: () => void;
    ontoggleright: () => void;
  };

  type Action = {
    id: string;
    label: string;
    shortcut: string;
    enabled: boolean;
    run: () => void;
  };

  const {
    canUndo,
    canRedo,
    busy,
    onundo,
    onredo,
    onopen,
    ontoggleleft,
    ontoggleright
  }: Props = $props();

  const flow = useSvelteFlow();

  const ZOOM_MS = 150;
  const FIT_MS = 200;
  const SAVED_MS = 1500;
  const MENU_WIDTH = 200;
  const MENU_HEIGHT = 120;
  const MENU_IDS = ['undo', 'redo', 'fit'];
  const CTRL_KEYS: Record<string, string> = {
    y: 'redo',
    o: 'open',
    '=': 'zoom-in',
    '+': 'zoom-in',
    '-': 'zoom-out',
    _: 'zoom-out',
    '0': 'fit'
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
    ]
  };

  let menu = $state<{ x: number; y: number } | null>(null);
  let saved = $state(false);
  let savedTimer = 0;

  const actions = $derived<Action[]>([
    { id: 'open', label: 'Open file', shortcut: 'Ctrl+O', enabled: !busy, run: onopen },
    { id: 'undo', label: 'Undo', shortcut: 'Ctrl+Z', enabled: canUndo && !busy, run: onundo },
    { id: 'redo', label: 'Redo', shortcut: 'Ctrl+Shift+Z', enabled: canRedo && !busy, run: onredo },
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
    }
  ]);

  const entries = $derived(actions.filter((action) => MENU_IDS.includes(action.id)));

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

  function onKeydown(event: KeyboardEvent) {
    if (event.key === 'Escape') {
      menu = null;
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
    if (key === '[') ontoggleleft();
    else if (key === ']') ontoggleright();
    else return;
    event.preventDefault();
  }

  function onContextMenu(event: MouseEvent) {
    menu = null;
    const target = event.target;
    if (!(target instanceof Element) || !target.closest('.svelte-flow__pane')) return;
    event.preventDefault();
    menu = {
      x: Math.min(event.clientX, window.innerWidth - MENU_WIDTH),
      y: Math.min(event.clientY, window.innerHeight - MENU_HEIGHT)
    };
  }
</script>

<svelte:window onkeydown={onKeydown} oncontextmenu={onContextMenu} onclick={() => (menu = null)} />

<div class="bar" data-testid="top-bar">
  <img src="/brand/cler_mark.png" alt="cler" width="22" height="22" />
  <span class="wordmark">cler</span>
  <span class="tagline">flowgraph editor</span>
  <span class="grow"></span>
  {#each actions as action (action.id)}
    {#if action.id === 'zoom-out'}
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

{#if menu}
  <div class="menu" data-testid="context-menu" style="left: {menu.x}px; top: {menu.y}px">
    {#each entries as entry (entry.id)}
      <button data-testid="menu-{entry.id}" disabled={!entry.enabled} onclick={() => act(entry)}>
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
    font-size: 10px;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--faint);
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
  .key {
    font-family: var(--mono);
    font-size: 10.5px;
    color: var(--faint);
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
