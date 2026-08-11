<script lang="ts">
  import {
    Background,
    MiniMap,
    SvelteFlow,
    type EdgeTypes,
    type NodeTypes,
    type Viewport
  } from '@xyflow/svelte';
  import '@xyflow/svelte/dist/style.css';
  import { tick, untrack } from 'svelte';
  import Actions, {
    type Alert,
    type EdgeInfo,
    type FitPadding,
    type Tasks
  } from './lib/Actions.svelte';
  import AddBlock from './lib/AddBlock.svelte';
  import Assistant from './lib/Assistant.svelte';
  import BlockNode from './lib/BlockNode.svelte';
  import Inspector from './lib/Inspector.svelte';
  import Palette from './lib/Palette.svelte';
  import { type RailTab } from './lib/RailTabs.svelte';
  import RoutedEdge from './lib/RoutedEdge.svelte';
  import TypeLegend from './lib/TypeLegend.svelte';
  import { diffLines, historyOf, type Message, type Proposal, type Usage } from './lib/assistant';
  import {
    applyCommands,
    assistantAsk,
    assistantStatus,
    assistantStop,
    onAssistantDelta,
    onAssistantDone,
    onAssistantProposal,
    previewCommands,
    type AssistantProposal,
    type AssistantStatus,
    buildTarget,
    checkDocument,
    closeDocument,
    describeApplyError,
    errorRecord,
    findTarget,
    inTauri,
    loadFixture,
    loadPalette,
    onExternalChange,
    onTaskEnd,
    onTaskLine,
    openDocument,
    openInEditor,
    pickFile,
    queued,
    redoDocument,
    reloadDocument,
    runTarget,
    saveDocument,
    spansOf,
    stopTarget,
    undoDocument,
    type TargetInfo,
    type TaskKind
  } from './lib/backend';
  import {
    compileProblems,
    parseDiagnostics,
    placeDiagnostics,
    type Placed
  } from './lib/diagnostics';
  import { layout } from './lib/layout';
  import {
    addBlockCommand,
    addRefusal,
    connectPlan,
    DRAG_TYPE,
    missingRequiredFields,
    reconnectPlan,
    specFor,
    specOfBlock,
    specsFromSites,
    type AddForm,
    type BlockSpec,
    type ConnectPlan,
    type FieldRefusal,
    type Wire
  } from './lib/palette';
  import {
    anchorSpans,
    blockSpans,
    edgeAtId,
    edgeIndexById,
    mergeProjection,
    parsePortId,
    problemsOf,
    projectSite,
    readOnlyNotes,
    targetAt,
    typeColors,
    type BlockNode as BlockNodeType,
    type EdgePoint,
    type Problem,
    type RoutedEdge as RoutedEdgeType
  } from './lib/project';
  import {
    lineOfOffset,
    lineTextAt,
    siteLabel,
    siteViewIds,
    type Command,
    type DocumentState,
    type Site,
    type Span
  } from './lib/schema';
  import type { CodeMark } from './lib/code';
  import type CodeDrawer from './lib/CodeDrawer.svelte';
  import type { Tab } from './lib/CodeDrawer.svelte';
  import { fixtureNames } from './fixtures';
  import type { Outcome } from './lib/inspector';

  const nodeTypes: NodeTypes = { block: BlockNode as unknown as NodeTypes[string] };
  const edgeTypes: EdgeTypes = { routed: RoutedEdge as unknown as EdgeTypes[string] };

  const requestedFixture = (() => {
    const search = new URLSearchParams(window.location.search);
    return search.get('example') ?? search.get('fixture');
  })();

  function initialFixture(): string {
    const requested = requestedFixture;
    return requested && fixtureNames.includes(requested) ? requested : 'hello_world';
  }

  const desktop = inTauri();
  const NOTE_BROWSER = 'example mode — read-only viewer, editing needs the desktop shell';
  const NOTE_DESKTOP = 'example mode — read-only viewer — use Open file… to edit the real file';
  const viewerNote = desktop ? NOTE_DESKTOP : NOTE_BROWSER;
  const DISK_DRIFT = 'changed on disk';
  const DISK_NOTE = 'this file changed on disk — reload before building or running';
  const DRAFT_NOTE = 'save the draft before checking, building, or running';
  const NO_SITE = 'no flowgraph site found in this file';
  const DROP_HINT = 'dropped — release on an input port to connect';
  const NO_SHELL = 'the assistant runs on your machine — open the desktop shell to use it';
  const MOVED_ON = 'the graph moved on since that proposal — re-check it before applying';
  const RAIL_WIDTH = 44;
  const SIDEBAR_WIDTH = 280;
  const INSPECTOR_WIDTH = 320;
  const MINIMAP_MIN = 12;
  const BAR_HEIGHT = 40;
  const INSET = 12;
  const FIT_GAP = 4;
  const LEFT_PANEL = 'cler.panel.left';
  const RIGHT_PANEL = 'cler.panel.right';
  const DRAWER_PANEL = 'cler.panel.drawer';
  const DRAWER_HEIGHT = 'cler.panel.drawer.height';
  const LAYOUT_PREFIX = 'cler.layout.';
  const DEFAULT_DRAWER_HEIGHT = 260;
  const OUTPUT_LINES = 2000;
  const MIN_DRAWER_HEIGHT = 90;

  function storedOpen(key: string, fallback: boolean): boolean {
    const stored = localStorage.getItem(key);
    return stored === null ? fallback : stored !== 'closed';
  }

  function storeOpen(key: string, open: boolean): void {
    localStorage.setItem(key, open ? 'open' : 'closed');
  }

  function storedHeight(): number {
    const stored = Number(localStorage.getItem(DRAWER_HEIGHT));
    return Number.isFinite(stored) && stored >= MIN_DRAWER_HEIGHT ? stored : DEFAULT_DRAWER_HEIGHT;
  }

  const startFixture = initialFixture();

  let fixtureName = $state(startFixture);
  let doc = $state.raw<DocumentState>(loadFixture(startFixture));
  let opened = $state<string | null>(null);
  let siteIndex = $state(0);
  let selected = $state<string | null>(null);
  let changedOnDisk = $state(false);
  let nodes = $state.raw<BlockNodeType[]>([]);
  let edges = $state.raw<RoutedEdgeType[]>([]);
  let status = $state('');
  let viewKey = $state('');
  let leftOpen = $state(storedOpen(LEFT_PANEL, true));
  let rightOpen = $state(storedOpen(RIGHT_PANEL, false));
  let drawerOpen = $state(storedOpen(DRAWER_PANEL, false));
  let drawerHeight = $state(storedHeight());
  let stashed = $state.raw<[boolean, boolean, boolean] | null>(null);
  let failure = $state<string | null>(null);
  let drawer = $state<typeof CodeDrawer | null>(null);
  let actions = $state<Actions | null>(null);
  let inspector = $state<Inspector | null>(null);
  let adder = $state<AddBlock | null>(null);
  let specs = $state.raw<BlockSpec[]>([]);
  let selectedEdge = $state<string | null>(null);
  let focus = $state<Span | null>(null);
  let refusal = $state<{ block: string; spans: Span[] } | null>(null);
  let alert = $state.raw<Alert | null>(null);
  let tab = $state<Tab>('code');
  let target = $state.raw<TargetInfo | null>(null);
  let targetError = $state<string | null>(null);
  let diagLines = $state.raw<string[]>([]);
  let output = $state.raw<string[]>([]);
  let busy = $state<TaskKind | null>(null);
  let running = $state(false);
  let rightTab = $state<RailTab>('inspector');
  let keyStatus = $state.raw<AssistantStatus | null>(null);
  let chat = $state.raw<Message[]>([]);
  let pendingReply = $state<number | null>(null);
  let turns = 0;
  let generation = 0;
  let opening = 0;
  let alerted = 0;
  let pinned = new Map<string, EdgePoint>();
  const positionsByView = new Map<string, Map<string, EdgePoint>>();
  const viewportsByView = new Map<string, Viewport>();
  const loadedViews = new Set<string>();

  const editable = $derived(desktop && opened !== null);
  const site = $derived<Site | undefined>(doc.model.sites[siteIndex]);
  const viewIds = $derived(siteViewIds(doc.model.sites));
  const viewId = $derived(viewIds[siteIndex] ?? '');
  const notes = $derived(site ? readOnlyNotes(site) : []);
  const legend = $derived<[string, string][]>(site ? [...typeColors(site)] : []);
  const needsReload = $derived(changedOnDisk || doc.externalChange);
  const hits = $derived<Span[]>(
    focus ? [focus] : site && selected ? blockSpans(site, selected) : []
  );
  const marks = $derived<CodeMark[]>(
    notes.map((note) => ({ span: note.span, reason: note.reason }))
  );
  const anchors = $derived<Span[]>(anchorSpans(doc.model.sites));
  const siteAnchor = $derived<Span | null>(anchors[siteIndex] ?? null);
  const emptyState = $derived.by(() => {
    if (failure !== null) return { title: 'that file did not open', reason: failure };
    return { title: 'nothing to show yet', reason: NO_SITE };
  });
  const empty = $derived(failure !== null || doc.model.sites.length === 0);
  const fitPadding = $derived<FitPadding>({
    top: `${BAR_HEIGHT + INSET + FIT_GAP}px`,
    right: `${(rightOpen ? INSPECTOR_WIDTH : RAIL_WIDTH) + INSET + FIT_GAP}px`,
    bottom: `${INSET + FIT_GAP}px`,
    left: `${(leftOpen ? SIDEBAR_WIDTH : RAIL_WIDTH) + INSET + FIT_GAP}px`
  });
  const shownSpecs = $derived(editable ? specs : specsFromSites(doc.model.sites, doc.path));
  const problems = $derived<Problem[]>(problemsOf(site));
  const declared = $derived(site ? site.blocks.map((block) => block.var) : []);
  const selectedBlock = $derived(site?.blocks.find((block) => block.var === selected) ?? null);
  const chatSelection = $derived(
    selectedBlock
      ? { label: selectedBlock.display_name ?? selectedBlock.var, var: selectedBlock.var }
      : null
  );
  const selectedSpec = $derived.by(() => {
    const block = site?.blocks.find((candidate) => candidate.var === selected);
    return block ? specOfBlock(specs, block) : undefined;
  });
  const incompleteBlocks = $derived.by(() =>
    doc.model.sites.flatMap((candidate) =>
      candidate.blocks.flatMap((block) => {
        const spec = specOfBlock(specs, block);
        return spec && missingRequiredFields(block, spec).length > 0 ? [block.var] : [];
      })
    )
  );
  const incompleteNote = $derived(
    incompleteBlocks.length === 0
      ? null
      : `${incompleteBlocks.length} block${incompleteBlocks.length === 1 ? '' : 's'} missing required fields`
  );
  const diagnostics = $derived<Placed[]>(
    placeDiagnostics(parseDiagnostics(diagLines), doc.path, doc.source, doc.model.sites)
  );
  const compiled = $derived<Problem[]>(compileProblems(diagnostics));
  const blocked = $derived<string | null>(
    !editable ? viewerNote : needsReload ? DISK_NOTE : null
  );
  const taskBlocked = $derived(blocked ?? (doc.dirty ? DRAFT_NOTE : null));
  const tasks = $derived<Tasks>({
    check: {
      enabled: taskBlocked === null && targetError === null && busy === null,
      hint: taskBlocked ?? targetError ?? 'syntax-check this file with g++ (F7)'
    },
    build: {
      enabled: taskBlocked === null && busy === null && target?.available === true,
      hint:
        taskBlocked ?? target?.reason ?? targetError ?? 'build this example with cmake (Ctrl+B)'
    },
    run: {
      enabled:
        running ||
        (taskBlocked === null &&
          incompleteNote === null &&
          busy === null &&
          target?.available === true),
      hint:
        running
          ? 'stop the running example (Ctrl+R)'
          : taskBlocked ??
            incompleteNote ??
            target?.reason ??
            targetError ??
            'run the built example (Ctrl+R)'
    }
  });

  $effect(() => storeOpen(LEFT_PANEL, leftOpen));
  $effect(() => storeOpen(RIGHT_PANEL, rightOpen));
  $effect(() => storeOpen(DRAWER_PANEL, drawerOpen));
  $effect(() => localStorage.setItem(DRAWER_HEIGHT, String(drawerHeight)));

  $effect(() => {
    if (!desktop) return;
    const name = fixtureName;
    void openExample(name);
  });

  $effect(() => {
    if (!drawerOpen || untrack(() => drawer)) return;
    void import('./lib/CodeDrawer.svelte').then((module) => (drawer = module.default));
  });

  $effect(() => {
    if (!editable) return;
    const pending = onExternalChange((path) => {
      if (path === doc.path) changedOnDisk = true;
    });
    return () => {
      void pending.then((unlisten) => unlisten());
    };
  });

  $effect(() => {
    const path = doc.path;
    if (!editable) {
      target = null;
      targetError = null;
      return;
    }
    let stale = false;
    void findTarget(path).then(
      (found) => {
        if (stale) return;
        target = found;
        targetError = null;
      },
      (error: unknown) => {
        if (stale) return;
        target = null;
        targetError = describeApplyError(error);
      }
    );
    return () => {
      stale = true;
    };
  });

  $effect(() => {
    if (!editable) return;
    const streams = onTaskLine((kind, payload) => {
      if (payload.path !== doc.path) return;
      output = [...output, payload.line].slice(-OUTPUT_LINES);
      if (kind !== 'run') diagLines = [...diagLines, payload.line];
    });
    const ends = onTaskEnd((kind, payload) => {
      if (payload.path !== doc.path) return;
      if (kind === 'run') running = false;
      else busy = null;
      output = [...output, `— ${kind} finished (exit ${payload.code ?? 'signal'})`];
    });
    return () => {
      void Promise.all([streams, ends]).then((pending) => {
        for (const unlisten of pending.flat()) unlisten();
      });
    };
  });

  $effect(() => {
    void refreshAssistant();
  });

  $effect(() => {
    if (!desktop) return;
    const deltas = onAssistantDelta((payload) => {
      if (payload.path !== doc.path || pendingReply === null) return;
      const target = pendingReply;
      chat = chat.map((message) =>
        message.id === target ? { ...message, text: message.text + payload.text } : message
      );
    });
    const ends = onAssistantDone((payload) => {
      if (payload.path !== doc.path) return;
      closeReply(payload.usage, payload.error);
    });
    const plans = onAssistantProposal((payload) => void attachProposal(payload));
    return () => {
      void Promise.all([deltas, ends, plans]).then((pending) => {
        for (const unlisten of pending) unlisten();
      });
    };
  });

  $effect(() => {
    const current = site;
    const key = current ? `${doc.path}#${viewId}` : '';
    if (!current) {
      nodes = [];
      edges = [];
      viewKey = '';
      return;
    }
    const fresh = projectSite(current, specs, editable, selected);
    if (untrack(() => viewKey) === key) {
      const merged = mergeProjection(untrack(() => ({ nodes, edges })), fresh, pinned);
      nodes = merged.nodes;
      edges = merged.edges;
      rememberPositions(key, merged.nodes, true);
      return;
    }
    let stale = false;
    pinned = new Map();
    layout(fresh).then(async (laid) => {
      if (stale) return;
      nodes = restorePositions(key, laid.nodes);
      edges = laid.edges;
      rememberPositions(key, nodes, true);
      viewKey = key;
      await tick();
      if (stale) return;
      void actions?.showView(viewportsByView.get(key) ?? null);
    });
    return () => {
      stale = true;
    };
  });

  function rememberPositions(
    key: string,
    current: { id: string; position: EdgePoint }[],
    complete = false
  ): void {
    if (!key) return;
    loadLayout(key);
    const positions = complete
      ? new Map<string, EdgePoint>()
      : (positionsByView.get(key) ?? new Map<string, EdgePoint>());
    for (const node of current) {
      positions.set(node.id, { x: node.position.x, y: node.position.y });
    }
    positionsByView.set(key, positions);
    storeLayout(key);
  }

  function restorePositions(key: string, current: BlockNodeType[]): BlockNodeType[] {
    loadLayout(key);
    const positions = positionsByView.get(key);
    if (!positions) return current;
    return current.map((node) => {
      const position = positions.get(node.id);
      return position ? { ...node, position: { x: position.x, y: position.y } } : node;
    });
  }

  function rememberViewport(key: string, viewport: Viewport): void {
    if (!key) return;
    loadLayout(key);
    viewportsByView.set(key, { x: viewport.x, y: viewport.y, zoom: viewport.zoom });
    storeLayout(key);
  }

  function loadLayout(key: string): void {
    if (loadedViews.has(key)) return;
    loadedViews.add(key);
    try {
      const stored = JSON.parse(localStorage.getItem(LAYOUT_PREFIX + key) ?? '{}') as {
        positions?: Record<string, EdgePoint>;
        viewport?: Viewport;
      };
      const positions = new Map<string, EdgePoint>();
      for (const [id, point] of Object.entries(stored.positions ?? {})) {
        if (Number.isFinite(point.x) && Number.isFinite(point.y)) positions.set(id, point);
      }
      if (positions.size > 0) positionsByView.set(key, positions);
      const viewport = stored.viewport;
      if (
        viewport &&
        Number.isFinite(viewport.x) &&
        Number.isFinite(viewport.y) &&
        Number.isFinite(viewport.zoom)
      ) {
        viewportsByView.set(key, viewport);
      }
    } catch {
      localStorage.removeItem(LAYOUT_PREFIX + key);
    }
  }

  function storeLayout(key: string): void {
    const positions = Object.fromEntries(positionsByView.get(key) ?? []);
    const viewport = viewportsByView.get(key) ?? null;
    localStorage.setItem(LAYOUT_PREFIX + key, JSON.stringify({ positions, viewport }));
  }

  function clampContext() {
    const count = doc.model.sites.length;
    if (count === 0) {
      siteIndex = 0;
      selected = null;
      return;
    }
    if (siteIndex >= count || siteIndex < 0) siteIndex = count - 1;
    if (count === 1) siteIndex = 0;
    const current = doc.model.sites[siteIndex];
    if (selected && !current?.blocks.some((block) => block.var === selected)) selected = null;
    if (selectedEdge && current && edgeIndexById(current, selectedEdge) === null) {
      selectedEdge = null;
    }
  }

  function adopt(next: DocumentState) {
    if (next.path === doc.path && next.revision < doc.revision) return;
    doc = next;
    clampContext();
  }

  function install(next: DocumentState, fresh: boolean) {
    if (fresh && pendingReply !== null) void assistantStop(doc.path).catch(() => undefined);
    doc = next;
    generation += 1;
    changedOnDisk = false;
    refusal = null;
    focus = null;
    void refreshPalette(next.path);
    if (fresh) {
      siteIndex = 0;
      selected = null;
      selectedEdge = null;
      chat = [];
      pendingReply = null;
    }
    clampContext();
    inspector?.discardDrafts();
    diagLines = [];
    output = [];
    busy = null;
    running = false;
    status = '';
  }

  function reset(next: DocumentState) {
    install(next, true);
  }

  function announce(message: string) {
    status = message;
    alerted += 1;
    alert = { text: message, at: alerted, tone: 'error' };
  }

  function hint(message: string) {
    alerted += 1;
    alert = { text: message, at: alerted, tone: 'note' };
  }

  async function refreshAssistant() {
    if (!desktop) {
      keyStatus = { available: false, model: '—', reason: NO_SHELL };
      return;
    }
    try {
      keyStatus = await assistantStatus();
    } catch (error) {
      keyStatus = { available: false, model: '—', reason: describeApplyError(error) };
    }
  }

  function closeReply(usage: Usage | null, error: string | null) {
    pendingReply = null;
    const last = chat.at(-1);
    if (!last || last.role !== 'assistant') return;
    chat = [...chat.slice(0, -1), { ...last, usage: usage ?? last.usage, error: error ?? last.error }];
  }

  async function askAssistant(question: string) {
    if (!editable) {
      announce(viewerNote);
      return;
    }
    if (pendingReply !== null) return;
    const history = historyOf(chat);
    const asked: Message = {
      id: ++turns,
      role: 'user',
      text: question,
      usage: null,
      error: null,
      proposal: null
    };
    const reply: Message = {
      id: ++turns,
      role: 'assistant',
      text: '',
      usage: null,
      error: null,
      proposal: null
    };
    chat = [...chat, asked, reply];
    pendingReply = reply.id;
    try {
      await assistantAsk(doc.path, question, history);
    } catch (error) {
      closeReply(null, describeApplyError(error));
    }
  }

  async function vetted(plan: Proposal, planned: number): Promise<Proposal> {
    try {
      const checked = await previewCommands(doc.path, plan.commands, planned);
      return {
        ...plan,
        baseRevision: planned,
        diff: diffLines(checked.diff),
        splices: checked.summary.splices,
        refusal: null,
        state: 'ready'
      };
    } catch (error) {
      const record = errorRecord(error);
      if (record?.error === 'references_outside_graph' && typeof record.block === 'string') {
        refusal = { block: record.block, spans: spansOf(record) };
      }
      return {
        ...plan,
        baseRevision: planned,
        diff: [],
        splices: 0,
        refusal: describeApplyError(error),
        state: 'refused'
      };
    }
  }

  async function attachProposal(payload: AssistantProposal) {
    if (!editable || payload.path !== doc.path) return;
    const planned = doc.revision;
    const plan = await vetted(
      {
        rationale: payload.rationale,
        commands: payload.commands,
        baseRevision: planned,
        dropped: payload.dropped,
        diff: [],
        splices: 0,
        refusal: null,
        state: 'ready',
        appliedAt: null
      },
      planned
    );
    const last = chat.at(-1);
    if (last && last.role === 'assistant' && last.proposal === null) {
      chat = [...chat.slice(0, -1), { ...last, proposal: plan }];
      return;
    }
    chat = [
      ...chat,
      { id: ++turns, role: 'assistant', text: '', usage: null, error: null, proposal: plan }
    ];
  }

  function planOf(id: number): Proposal | null {
    return chat.find((message) => message.id === id)?.proposal ?? null;
  }

  function setProposal(id: number, next: Proposal) {
    chat = chat.map((message) => (message.id === id ? { ...message, proposal: next } : message));
  }

  async function acceptProposal(id: number) {
    const plan = planOf(id);
    if (!plan || plan.state !== 'ready') return;
    if (plan.baseRevision !== doc.revision) {
      announce(MOVED_ON);
      return;
    }
    const outcome = await submitAll(plan.commands);
    const settled = planOf(id);
    if (!settled) return;
    if (!outcome.ok) {
      setProposal(id, { ...settled, refusal: outcome.message, state: 'refused' });
      return;
    }
    setProposal(id, { ...settled, state: 'accepted', appliedAt: doc.revision });
  }

  function rejectProposal(id: number) {
    const plan = planOf(id);
    if (!plan || plan.state === 'accepted') return;
    setProposal(id, { ...plan, state: 'rejected' });
  }

  async function replanProposal(id: number) {
    const plan = planOf(id);
    if (!plan || plan.state !== 'ready') return;
    setProposal(id, await vetted(plan, doc.revision));
  }

  function stopAssistant() {
    pendingReply = null;
    void assistantStop(doc.path).catch(() => undefined);
  }

  function pickRailTab(next: RailTab) {
    rightTab = next;
    rightOpen = true;
  }

  function toggleAssistant() {
    if (rightOpen && rightTab === 'assistant') {
      rightOpen = false;
      return;
    }
    rightTab = 'assistant';
    rightOpen = true;
  }

  function toggleChrome() {
    const previous = stashed;
    if (previous) {
      leftOpen = previous[0];
      rightOpen = previous[1];
      drawerOpen = previous[2];
      stashed = null;
      return;
    }
    if (!leftOpen && !rightOpen && !drawerOpen) {
      leftOpen = true;
      return;
    }
    stashed = [leftOpen, rightOpen, drawerOpen];
    leftOpen = false;
    rightOpen = false;
    drawerOpen = false;
  }

  async function attempt(
    action: (path: string) => Promise<DocumentState>,
    take: (next: DocumentState) => void
  ): Promise<Outcome> {
    if (!editable) return refuse(viewerNote);
    const era = generation;
    try {
      const next = await action(doc.path);
      status = '';
      take(next);
      return { ok: true };
    } catch (error) {
      const message = describeApplyError(error);
      if (message.includes(DISK_DRIFT) && era === generation) changedOnDisk = true;
      announce(message);
      return { ok: false, message, record: errorRecord(error) };
    }
  }

  function run(action: (path: string) => Promise<DocumentState>): Promise<Outcome> {
    const path = doc.path;
    return queued(path, () => attempt(action, adopt));
  }

  async function resync(message: string) {
    try {
      install(await openDocument(doc.path), false);
    } catch {
      return;
    }
    status = message;
  }

  async function reload() {
    const outcome = await attempt(reloadDocument, (next) => install(next, false));
    if (!outcome.ok) changedOnDisk = true;
  }

  async function save() {
    if (document.activeElement instanceof HTMLElement) document.activeElement.blur();
    const outcome = await run(saveDocument);
    if (outcome.ok) hint('saved to source');
  }

  function discardOnReload() {
    inspector?.discardDrafts();
  }

  async function submitAll(commands: Command[]): Promise<Outcome> {
    if (commands.length === 0) return { ok: true };
    const planned = doc.revision;
    const outcome = await run((path) => applyCommands(path, commands, planned));
    if (!outcome.ok && outcome.record?.error === 'revision_mismatch') {
      await resync(outcome.message);
    }
    return outcome;
  }

  function submit(command: Command): Promise<Outcome> {
    return submitAll([command]);
  }

  function refuse(message: string): Outcome {
    announce(message);
    return { ok: false, message, record: null };
  }

  function submitPlan(plan: ConnectPlan): Promise<Outcome> {
    if ('refusal' in plan) return Promise.resolve(refuse(plan.refusal));
    return submitAll(plan.commands);
  }

  async function refreshPalette(path: string) {
    if (!editable) {
      specs = [];
      return;
    }
    try {
      specs = await loadPalette(path);
    } catch {
      specs = [];
    }
  }

  async function openFile() {
    if (!desktop) {
      announce('file dialog needs the desktop shell — pick an example below');
      return;
    }
    status = '';
    try {
      const path = await pickFile();
      if (!path) return;
      const request = ++opening;
      const previous = opened;
      const next = await openDocument(path);
      if (request !== opening) return;
      opened = path;
      failure = null;
      if (previous && previous !== path) void closeDocument(previous).catch(() => undefined);
      reset(next);
    } catch (error) {
      failure = describeApplyError(error);
      announce(failure);
    }
  }

  async function openExample(name: string) {
    const request = ++opening;
    try {
      const next = await openDocument(loadFixture(name).path);
      if (request !== opening) return;
      const previous = opened;
      opened = next.path;
      failure = null;
      if (previous && previous !== next.path) void closeDocument(previous).catch(() => undefined);
      reset(next);
    } catch (error) {
      if (request !== opening) return;
      failure = describeApplyError(error);
      announce(failure);
    }
  }

  function openFixture() {
    if (desktop) return;
    const previous = opened;
    opened = null;
    failure = null;
    if (previous) void closeDocument(previous).catch(() => undefined);
    reset(loadFixture(fixtureName));
  }

  function selectNode(id: string) {
    selected = id;
    selectedEdge = null;
    focus = null;
    rightOpen = true;
  }

  function selectEdge(id: string) {
    selectedEdge = id;
    focus = null;
    edges = edges.map((edge) => ({ ...edge, selected: edge.id === id }));
  }

  function clearSelection() {
    selected = null;
    selectedEdge = null;
    focus = null;
  }

  function edgeInfo(id: string): EdgeInfo | null {
    const edge = site ? edgeAtId(site, id) : null;
    if (!edge) return null;
    const type = edge.type_conflict
      ? `type conflict: ${edge.source_type ?? '?'} → ${edge.sample_type ?? '?'}`
      : (edge.sample_type ?? edge.source_type ?? 'type unresolved');
    return {
      title: `${edge.from} → ${edge.to}.${edge.port.name}${edge.port.index === null ? '' : `[${edge.port.index}]`}`,
      detail: type,
      editable: edge.editable,
      reason: edge.read_only_reason ? `this wire is read-only: ${edge.read_only_reason.replace(/_/g, ' ')}` : null
    };
  }

  function wire(connection: {
    source: string;
    target: string;
    targetHandle?: string | null;
  }): void {
    if (!site) return;
    const slot = parsePortId(connection.targetHandle ?? 'in');
    const wanted: Wire = {
      from: connection.source,
      to: connection.target,
      port: slot.port,
      portIndex: slot.index
    };
    void submitPlan(connectPlan(siteIndex, site, specs, wanted));
  }

  function rewire(
    previous: { id: string },
    next: { source: string; target: string; targetHandle?: string | null }
  ): void {
    if (!site) return;
    const index = edgeIndexById(site, previous.id);
    if (index === null) return;
    const slot = parsePortId(next.targetHandle ?? 'in');
    void submitPlan(
      reconnectPlan(siteIndex, site, specs, index, {
        from: next.source,
        to: next.target,
        port: slot.port,
        portIndex: slot.index
      })
    );
  }

  function disconnect(id: string): void {
    if (!site) return;
    const edge = edgeAtId(site, id);
    const index = edgeIndexById(site, id);
    if (!edge || index === null) return;
    if (!edge.editable) {
      refuse(edgeInfo(id)?.reason ?? 'this wire is read-only');
      return;
    }
    selectedEdge = null;
    void submit({ command: 'disconnect', site: siteIndex, edge: index });
  }

  function removeFromGraph(block: string): void {
    void submit({ command: 'remove_from_graph', site: siteIndex, block });
  }

  async function deleteBlock(block: string): Promise<void> {
    const outcome = await submit({ command: 'delete_block', site: siteIndex, block });
    if (outcome.ok) return;
    if (outcome.record?.error !== 'references_outside_graph') return;
    refusal = { block, spans: spansOf(outcome.record) };
  }

  function jumpTo(span: Span) {
    focus = span;
    drawerOpen = true;
    tab = 'code';
  }

  async function task(kind: TaskKind, action: (path: string) => Promise<void>) {
    output = [];
    if (kind !== 'run') diagLines = [];
    if (kind === 'run') running = true;
    else busy = kind;
    tab = kind === 'check' ? 'diagnostics' : 'output';
    drawerOpen = true;
    try {
      await action(doc.path);
    } catch (error) {
      if (kind === 'run') running = false;
      else busy = null;
      announce(describeApplyError(error));
    }
  }

  async function toggleRun() {
    if (!running) {
      await task('run', runTarget);
      return;
    }
    try {
      await stopTarget(doc.path);
    } catch (error) {
      announce(describeApplyError(error));
    }
  }

  function pickDiagnostic(entry: Placed) {
    if (entry.site !== null) siteIndex = entry.site;
    if (entry.block) selectNode(entry.block);
    if (entry.span) jumpTo(entry.span);
  }

  function pickProblem(problem: Problem) {
    if (problem.edge) {
      if (problem.block) selected = problem.block;
      selectEdge(problem.edge);
      return;
    }
    if (problem.block) selectNode(problem.block);
    jumpTo(problem.span);
  }

  async function addBlock(
    spec: BlockSpec,
    form: AddForm,
    at: EdgePoint
  ): Promise<FieldRefusal | null> {
    const varName = form.varName.trim();
    pinned.set(varName, at);
    const outcome = await submit(addBlockCommand(siteIndex, spec, form));
    if (outcome.ok) {
      rightTab = 'inspector';
      selectNode(varName);
      return null;
    }
    pinned.delete(varName);
    return addRefusal(outcome.record, form) ?? { field: null, message: outcome.message };
  }

  function placeSpec(spec: BlockSpec) {
    adder?.placeAt(window.innerWidth / 2, window.innerHeight / 2, spec);
  }

  function droppedSpec(event: DragEvent): BlockSpec | null {
    const name = event.dataTransfer?.getData(DRAG_TYPE);
    return name ? (specFor(shownSpecs, name) ?? null) : null;
  }

  function onDrop(event: DragEvent) {
    const spec = droppedSpec(event);
    if (!spec) return;
    event.preventDefault();
    if (!editable) {
      announce(viewerNote);
      return;
    }
    adder?.placeAt(event.clientX, event.clientY, spec);
  }

  function onDragOver(event: DragEvent) {
    if (!event.dataTransfer?.types.includes(DRAG_TYPE)) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'copy';
  }

  function addHere(clientX: number, clientY: number) {
    adder?.openAt(clientX, clientY);
  }

  function declarationOf(blockVar: string): Span | null {
    return site?.blocks.find((block) => block.var === blockVar)?.span ?? null;
  }

  function viewSource(blockVar: string) {
    selectNode(blockVar);
    drawerOpen = true;
  }

  async function copyDeclaration(blockVar: string) {
    const span = declarationOf(blockVar);
    if (!span) return;
    try {
      await navigator.clipboard.writeText(doc.source.slice(span.start, span.end));
      status = '';
    } catch {
      announce('the clipboard is not available here');
    }
  }

  async function openEditor(blockVar: string) {
    if (!desktop) {
      announce('opening an editor needs the desktop shell');
      return;
    }
    if (!editable) {
      announce(viewerNote);
      return;
    }
    const span = declarationOf(blockVar);
    if (!span) return;
    try {
      await openInEditor(doc.path, lineOfOffset(doc.source, span.start));
      status = '';
    } catch (error) {
      announce(describeApplyError(error));
    }
  }

  function readable(text: string): string {
    return text.replace(/_/g, ' ');
  }

  function pickInCode(offset: number) {
    const target = targetAt(doc.model.sites, offset);
    if (!target) return;
    siteIndex = target.siteIndex;
    selectNode(target.block);
  }

</script>

<div
  class="shell"
  style="--rail-left: {leftOpen ? SIDEBAR_WIDTH : RAIL_WIDTH}px; --rail-right: {rightOpen
    ? INSPECTOR_WIDTH
    : RAIL_WIDTH}px"
>
  <aside class="sidebar" class:collapsed={!leftOpen}>
    <div class="head">
      <h1>Document</h1>
      <button
        class="toggle"
        data-testid="toggle-left"
        aria-expanded={leftOpen}
        aria-label={leftOpen ? 'Collapse sidebar' : 'Expand sidebar'}
        title={leftOpen ? 'Collapse sidebar  [' : 'Expand sidebar  ['}
        onclick={() => (leftOpen = !leftOpen)}>{leftOpen ? '‹' : '›'}</button
      >
    </div>

    <div class="body">
      <section>
        <h2>File</h2>
        <div class="path" title={doc.path}>{doc.path.split('/').pop() ?? doc.path}</div>
        <dl>
          <dt>sites</dt>
          <dd>{doc.model.sites.length}</dd>
          <dt>revision</dt>
          <dd>{doc.revision}</dd>
          <dt>schema</dt>
          <dd>{doc.model.version}</dd>
        </dl>
      </section>

      {#if doc.model.sites.length > 1}
        <section>
          <h2>Site</h2>
          <select data-testid="site-select" bind:value={siteIndex}>
            {#each doc.model.sites as candidate, i (viewIds[i] ?? i)}
              <option value={i}>{siteLabel(candidate)}</option>
            {/each}
          </select>
        </section>
      {/if}

      {#if site}
        <section>
          <h2>Graph</h2>
          <dl>
            <dt>function</dt>
            <dd>{site.function}()</dd>
            <dt>flowgraph</dt>
            <dd>{site.flowgraph_var}</dd>
            <dt>blocks</dt>
            <dd>{site.blocks.length}</dd>
            <dt>edges</dt>
            <dd>{site.edges.length}</dd>
            <dt>unwired</dt>
            <dd>{site.blocks.filter((block) => !block.in_graph).length}</dd>
          </dl>
        </section>
      {/if}

      {#if site}
        <section>
          <h2>Read-only ({notes.length})</h2>
          {#if notes.length === 0}
            <p class="muted">everything in this site is editable</p>
          {:else}
            <ul class="notes">
              {#each notes as note (note.element + note.reason)}
                <li>
                  <span class="el">{note.element}</span><span class="reason"
                    >{readable(note.reason)}</span
                  >
                </li>
              {/each}
            </ul>
          {/if}
        </section>
      {/if}

      <section>
        <h2 data-testid="examples-head">Examples</h2>
        <select data-testid="example-select" bind:value={fixtureName} onchange={openFixture}>
          {#each fixtureNames as name (name)}
            <option value={name}>{name}</option>
          {/each}
        </select>
      </section>

      {#if status}
        <p class="status" data-testid="status">{status}</p>
      {/if}

      <span class="attribution">CaribouLabs</span>
    </div>
  </aside>

  <main>
    {#if needsReload}
      <div class="banner" data-testid="reload-banner">
        <span
          >This file changed on disk. Reloading replaces the open document and
          <strong>discards the undo history</strong>.</span
        >
        <button onpointerdown={discardOnReload} onclick={reload}>Reload</button>
      </div>
    {/if}

    <div class="canvas" data-canvas tabindex="-1">
      <SvelteFlow
          bind:nodes
          bind:edges
          {nodeTypes}
          {edgeTypes}
          colorMode="dark"
          fitViewOptions={{ padding: fitPadding }}
          nodesConnectable={editable}
          elementsSelectable={true}
          deleteKey={null}
          minZoom={0.1}
          proOptions={{ hideAttribution: true }}
          ondrop={onDrop}
          ondragover={onDragOver}
          onnodedragstop={({ nodes: moved }) => rememberPositions(viewKey, moved)}
          onmoveend={(_event, viewport) => rememberViewport(viewKey, viewport)}
          onnodeclick={({ node }) => selectNode(node.id)}
          onedgeclick={({ edge }) => selectEdge(edge.id)}
          onpaneclick={clearSelection}
          onconnectend={(_event, state) => {
            if (!state.toHandle) hint(DROP_HINT);
          }}
          onbeforeconnect={(connection) => {
            wire(connection);
            return null;
          }}
          onbeforereconnect={(next, previous) => {
            rewire(previous, next);
            return null;
          }}
        >
          <Background bgColor="var(--bg-0)" patternColor="var(--border)" gap={18} size={2} />
          <Actions
            bind:this={actions}
            canUndo={doc.canUndo}
            canRedo={doc.canRedo}
            canSave={editable && !needsReload}
            canOpenEditor={editable}
            canEdit={editable}
            dirty={doc.dirty}
            demo={!editable}
            editNote={viewerNote}
            saveNote={!editable
              ? viewerNote
              : needsReload
                ? 'reload the external change before saving'
                : doc.dirty
                  ? 'write the draft to the source file (Ctrl+S)'
                  : 'save the current document (Ctrl+S)'}
            {alert}
            {leftOpen}
            {rightOpen}
            {fitPadding}
            selectedNode={selected}
            {selectedEdge}
            {problems}
            {compiled}
            {tasks}
            {running}
            edgeAt={edgeInfo}
            oncheck={() => void task('check', checkDocument)}
            onbuild={() => void task('build', buildTarget)}
            onrun={() => void toggleRun()}
            onsave={() => void save()}
            onundo={() => void run(undoDocument)}
            onredo={() => void run(redoDocument)}
            onopen={() => void openFile()}
            ontoggleleft={() => (leftOpen = !leftOpen)}
            ontoggleright={() => (rightOpen = !rightOpen)}
            ontoggledrawer={() => (drawerOpen = !drawerOpen)}
            ontoggleassistant={toggleAssistant}
            ontogglechrome={toggleChrome}
            onviewsource={viewSource}
            oncopydeclaration={(block) => void copyDeclaration(block)}
            onopeneditor={(block) => void openEditor(block)}
            onremove={removeFromGraph}
            ondeleteblock={(block) => void deleteBlock(block)}
            ondisconnect={disconnect}
            onaddhere={addHere}
            onproblem={pickProblem}
          />
          <AddBlock
            bind:this={adder}
            specs={shownSpecs}
            documentPath={doc.path}
            taken={declared}
            onadd={addBlock}
          />
          <TypeLegend entries={legend} />
          {#if nodes.length >= MINIMAP_MIN}
            <MiniMap bgColor="var(--bg-1)" maskColor="var(--scrim)" nodeColor="var(--border-hi)" />
          {/if}
      </SvelteFlow>

      {#if empty}
        <div class="empty" data-testid="empty-state">
          <h2>{emptyState.title}</h2>
          <p data-testid="empty-reason">{emptyState.reason}</p>
          <div class="choices">
            <button class="primary" data-testid="empty-open" onclick={openFile}
              >Open a .cpp file (Ctrl+O)</button
            >
            <label class="browse">
              Browse examples
              <select
                data-testid="empty-examples"
                bind:value={fixtureName}
                onchange={openFixture}
              >
                {#each fixtureNames as name (name)}
                  <option value={name}>{name}</option>
                {/each}
              </select>
            </label>
          </div>
        </div>
      {/if}

      {#if refusal}
        <div class="dialog" role="dialog" aria-modal="true" data-testid="delete-refusal">
          <h2>{refusal.block} cannot be deleted</h2>
          <p>
            {`Its declaration is still referenced in ${refusal.spans.length} ${refusal.spans.length === 1 ? 'place' : 'places'} outside the flowgraph.`}
            Remove those references first — the editor will not rewrite code it does not own.
          </p>
          <ul>
            {#each refusal.spans as span (span.start)}
              <li>
                <button data-reference={span.start} onclick={() => jumpTo(span)}>
                  <span class="line">line {lineOfOffset(doc.source, span.start)}</span>
                  <code>{lineTextAt(doc.source, span.start)}</code>
                </button>
              </li>
            {/each}
          </ul>
          <footer>
            <button data-testid="refusal-close" onclick={() => (refusal = null)}>Close</button>
          </footer>
        </div>
      {/if}

      {#if !drawerOpen}
        <button
          class="drawer-toggle"
          data-testid="drawer-toggle"
          aria-label="Expand code drawer"
          title="Expand code drawer  Ctrl+`"
          onclick={() => (drawerOpen = true)}>⌃ Code</button
        >
      {/if}

      {#if drawer}
        {@const Drawer = drawer}
        <Drawer
          open={drawerOpen}
          source={doc.source}
          path={doc.path}
          revision={doc.revision}
          readOnly={notes.length}
          {hits}
          {marks}
          {anchors}
          {siteAnchor}
          height={drawerHeight}
          {tab}
          {diagnostics}
          {output}
          {busy}
          onpick={pickInCode}
          ontoggle={() => (drawerOpen = !drawerOpen)}
          onheight={(next) => (drawerHeight = next)}
          ontab={(next: Tab) => (tab = next)}
          ondiagnostic={pickDiagnostic}
        />
      {/if}
    </div>
  </main>

  {#if rightTab === 'assistant'}
    <Assistant
      open={rightOpen}
      status={keyStatus}
      messages={chat}
      pending={pendingReply}
      selection={chatSelection}
      enabled={editable}
      note={viewerNote}
      revision={doc.revision}
      ontoggle={() => (rightOpen = !rightOpen)}
      ontab={pickRailTab}
      onask={(question) => void askAssistant(question)}
      onstop={stopAssistant}
      onrecheck={() => void refreshAssistant()}
      onaccept={(id) => void acceptProposal(id)}
      onreject={rejectProposal}
      onreplan={(id) => void replanProposal(id)}
    />
  {:else if rightTab === 'library'}
    <Palette
      specs={shownSpecs}
      documentPath={doc.path}
      enabled={editable}
      open={rightOpen}
      ontoggle={() => (rightOpen = !rightOpen)}
      ontab={pickRailTab}
      onpick={placeSpec}
    />
  {:else}
    <Inspector
      bind:this={inspector}
      path={doc.path}
      {site}
      {siteIndex}
      {selected}
      spec={selectedSpec}
      enabled={editable}
      {submit}
      open={rightOpen}
      ontoggle={() => (rightOpen = !rightOpen)}
      ontab={pickRailTab}
    />
  {/if}
</div>

<style>
  .shell {
    position: relative;
    height: 100%;
  }
  .sidebar {
    position: absolute;
    z-index: 8;
    top: calc(var(--bar-h) + var(--sp-3));
    left: var(--sp-3);
    bottom: var(--sp-3);
    width: var(--rail-left);
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
  h1 {
    margin: 0;
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  .collapsed h1 {
    display: none;
  }
  .toggle {
    margin-left: auto;
    flex: none;
    width: 26px;
    padding: var(--sp-0) 0;
    font-size: 14px;
    line-height: 1.1;
    color: var(--muted);
  }
  .collapsed .toggle {
    margin-left: 0;
  }
  .body {
    flex: 1;
    min-height: 0;
    width: 280px;
    padding: 0 var(--sp-3) var(--sp-3);
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: var(--sp-3);
    transition:
      opacity 120ms ease,
      visibility 150ms;
  }
  .collapsed .body {
    opacity: 0;
    visibility: hidden;
  }
  h2 {
    margin: 0 0 var(--sp-2);
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  .path {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
    word-break: break-all;
  }
  dl {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 0 var(--sp-3);
    margin: var(--sp-2) 0 0;
  }
  dt {
    color: var(--muted);
  }
  dd {
    margin: 0;
    font-family: var(--mono);
    font-size: 11px;
  }
  .notes {
    margin: 0;
    padding: 0;
    list-style: none;
    display: flex;
    flex-direction: column;
    gap: var(--sp-1);
  }
  .notes li {
    display: flex;
    flex-direction: column;
    border-left: 2px solid var(--faint);
    padding-left: var(--sp-2);
  }
  .el {
    font-size: 11px;
  }
  .reason {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .muted {
    margin: 0;
    color: var(--muted);
    font-size: 11px;
  }
  .status {
    margin: 0;
    padding: var(--sp-1) var(--sp-2);
    border: 1px solid var(--danger-border);
    background: var(--danger-bg);
    border-radius: var(--radius-sm);
    font-size: 11px;
    color: var(--danger-fg);
  }
  .attribution {
    margin-top: auto;
    padding-top: var(--sp-2);
    font-size: 11px;
    letter-spacing: 0.06em;
    color: var(--muted);
  }
  main {
    position: absolute;
    inset: 0;
    display: flex;
    flex-direction: column;
  }
  .canvas {
    position: relative;
    flex: 1;
    min-height: 0;
  }
  .drawer-toggle {
    position: absolute;
    z-index: 8;
    left: 50%;
    bottom: var(--sp-3);
    transform: translateX(-50%);
    padding: var(--sp-0) var(--sp-3);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border-color: var(--border);
    color: var(--muted);
    font-size: 11px;
  }
  .banner {
    display: flex;
    align-items: center;
    gap: var(--sp-3);
    padding: var(--sp-2) var(--sp-3);
    border-bottom: 1px solid var(--warn-border);
    background: var(--warn-bg);
    color: var(--fg);
    font-size: 12px;
  }
  .banner button {
    margin-left: auto;
    flex: none;
  }
  .empty {
    position: absolute;
    z-index: 6;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    width: min(420px, calc(100% - 2 * var(--sp-4)));
    padding: var(--sp-4);
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
  }
  .empty h2 {
    margin: 0;
    font-size: 14px;
    font-weight: 600;
    color: var(--fg);
    text-transform: none;
    letter-spacing: 0;
  }
  .empty p {
    margin: 0;
    font-size: 12px;
    color: var(--muted);
  }
  .choices {
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
    margin-top: var(--sp-2);
  }
  .browse {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    white-space: nowrap;
    font-size: 11px;
    color: var(--muted);
  }
  .dialog {
    position: absolute;
    top: 60px;
    left: 50%;
    transform: translateX(-50%);
    z-index: 25;
    width: min(560px, calc(100% - 2 * var(--sp-4)));
    max-height: 60%;
    overflow-y: auto;
    padding: var(--sp-3);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--danger-border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
  }
  .dialog h2 {
    margin: 0 0 var(--sp-2);
    font-size: 14px;
    font-weight: 600;
    color: var(--danger-fg);
    text-transform: none;
    letter-spacing: 0;
  }
  .dialog p {
    margin: 0 0 var(--sp-2);
    font-size: 12px;
    color: var(--fg);
  }
  .dialog ul {
    margin: 0;
    padding: 0;
    list-style: none;
    display: flex;
    flex-direction: column;
    gap: 0;
  }
  .dialog ul button {
    display: flex;
    gap: var(--sp-3);
    width: 100%;
    padding: var(--sp-0) var(--sp-2);
    background: transparent;
    border-color: transparent;
    text-align: left;
  }
  .dialog ul button:hover {
    background: var(--bg-2);
    border-color: transparent;
  }
  .dialog .line {
    flex: none;
    width: 62px;
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .dialog code {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--fg);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .dialog footer {
    display: flex;
    justify-content: flex-end;
    margin-top: var(--sp-3);
  }
  @media (prefers-reduced-motion: reduce) {
    .sidebar,
    .body {
      transition: none;
    }
  }

</style>
