<script lang="ts">
  import {
    Background,
    Controls,
    MiniMap,
    SvelteFlow,
    type EdgeTypes,
    type NodeTypes
  } from '@xyflow/svelte';
  import '@xyflow/svelte/dist/style.css';
  import { untrack } from 'svelte';
  import Actions, { type EdgeInfo } from './lib/Actions.svelte';
  import AddBlock from './lib/AddBlock.svelte';
  import BlockNode from './lib/BlockNode.svelte';
  import Inspector from './lib/Inspector.svelte';
  import Palette from './lib/Palette.svelte';
  import RoutedEdge from './lib/RoutedEdge.svelte';
  import TypeLegend from './lib/TypeLegend.svelte';
  import {
    applyCommands,
    closeDocument,
    describeApplyError,
    errorRecord,
    inTauri,
    loadFixture,
    loadPalette,
    onExternalChange,
    openDocument,
    openInEditor,
    pickFile,
    queued,
    redoDocument,
    reloadDocument,
    spansOf,
    undoDocument
  } from './lib/backend';
  import { layout } from './lib/layout';
  import {
    addBlockCommand,
    addRefusal,
    connectPlan,
    DRAG_TYPE,
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
  import { fixtureNames } from './fixtures';
  import type { Outcome } from './lib/inspector';

  const nodeTypes: NodeTypes = { block: BlockNode as unknown as NodeTypes[string] };
  const edgeTypes: EdgeTypes = { routed: RoutedEdge as unknown as EdgeTypes[string] };

  function initialFixture(): string {
    const search = new URLSearchParams(window.location.search);
    const requested = search.get('example') ?? search.get('fixture');
    return requested && fixtureNames.includes(requested) ? requested : 'hello_world';
  }

  const editable = inTauri();
  const DISK_DRIFT = 'changed on disk';
  const LEFT_PANEL = 'cler.panel.left';
  const RIGHT_PANEL = 'cler.panel.right';
  const BLOCKS_PANEL = 'cler.panel.blocks';
  const DRAWER_PANEL = 'cler.panel.drawer';
  const DRAWER_HEIGHT = 'cler.panel.drawer.height';
  const DEFAULT_DRAWER_HEIGHT = 260;
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
  let rightOpen = $state(storedOpen(RIGHT_PANEL, true));
  let blocksOpen = $state(storedOpen(BLOCKS_PANEL, true));
  let drawerOpen = $state(storedOpen(DRAWER_PANEL, false));
  let drawerHeight = $state(storedHeight());
  let drawer = $state<typeof CodeDrawer | null>(null);
  let inspector = $state<Inspector | null>(null);
  let adder = $state<AddBlock | null>(null);
  let specs = $state.raw<BlockSpec[]>([]);
  let selectedEdge = $state<string | null>(null);
  let focus = $state<Span | null>(null);
  let refusal = $state<{ block: string; spans: Span[] } | null>(null);
  let generation = 0;
  let pinned = new Map<string, EdgePoint>();

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
  const shownSpecs = $derived(editable ? specs : specsFromSites(doc.model.sites, doc.path));
  const problems = $derived<Problem[]>(problemsOf(site));
  const declared = $derived(site ? site.blocks.map((block) => block.var) : []);
  const selectedSpec = $derived.by(() => {
    const block = site?.blocks.find((candidate) => candidate.var === selected);
    return block ? specOfBlock(specs, block) : undefined;
  });

  $effect(() => storeOpen(LEFT_PANEL, leftOpen));
  $effect(() => storeOpen(RIGHT_PANEL, rightOpen));
  $effect(() => storeOpen(BLOCKS_PANEL, blocksOpen));
  $effect(() => storeOpen(DRAWER_PANEL, drawerOpen));
  $effect(() => localStorage.setItem(DRAWER_HEIGHT, String(drawerHeight)));

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
    const current = site;
    const key = current ? `${doc.path}#${viewId}` : '';
    if (!current) {
      nodes = [];
      edges = [];
      viewKey = '';
      return;
    }
    const fresh = projectSite(current, specs, editable);
    if (untrack(() => viewKey) === key) {
      const merged = mergeProjection(untrack(() => ({ nodes, edges })), fresh, pinned);
      nodes = merged.nodes;
      edges = merged.edges;
      return;
    }
    let stale = false;
    pinned = new Map();
    layout(fresh).then((laid) => {
      if (stale) return;
      nodes = laid.nodes;
      edges = laid.edges;
      viewKey = key;
    });
    return () => {
      stale = true;
    };
  });

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
    }
    clampContext();
    inspector?.discardDrafts();
    status = next.model.sites.length === 0 ? 'no flowgraph site found in this file' : '';
  }

  function reset(next: DocumentState) {
    install(next, true);
  }

  async function attempt(
    action: (path: string) => Promise<DocumentState>,
    take: (next: DocumentState) => void
  ): Promise<Outcome> {
    const era = generation;
    try {
      const next = await action(doc.path);
      status = '';
      take(next);
      return { ok: true };
    } catch (error) {
      const message = describeApplyError(error);
      if (message.includes(DISK_DRIFT) && era === generation) changedOnDisk = true;
      status = message;
      return { ok: false, message, record: errorRecord(error) };
    }
  }

  function run(action: (path: string) => Promise<DocumentState>): Promise<Outcome> {
    const path = doc.path;
    return queued(path, () => attempt(action, adopt));
  }

  async function reload() {
    const outcome = await attempt(reloadDocument, (next) => install(next, false));
    if (!outcome.ok) changedOnDisk = true;
  }

  function discardOnReload() {
    inspector?.discardDrafts();
  }

  const VIEWER_ONLY = 'example mode is a read-only viewer';

  async function submitAll(commands: Command[]): Promise<Outcome> {
    if (!editable) {
      status = VIEWER_ONLY;
      return { ok: false, message: VIEWER_ONLY, record: null };
    }
    if (commands.length === 0) return { ok: true };
    return run((path) => applyCommands(path, commands, doc.revision));
  }

  function submit(command: Command): Promise<Outcome> {
    return submitAll([command]);
  }

  function refuse(message: string): Outcome {
    status = message;
    return { ok: false, message, record: null };
  }

  function submitPlan(plan: ConnectPlan): Promise<Outcome> {
    if ('refusal' in plan) return Promise.resolve(refuse(plan.refusal));
    return submitAll(plan.commands);
  }

  async function refreshPalette(path: string) {
    if (!editable) return;
    try {
      specs = await loadPalette(path);
    } catch {
      specs = [];
    }
  }

  async function openFile() {
    if (!editable) {
      status = 'file dialog needs the desktop shell — pick an example below';
      return;
    }
    status = '';
    try {
      const path = await pickFile();
      if (!path) return;
      const previous = opened;
      const next = await openDocument(path);
      opened = path;
      if (previous && previous !== path) void closeDocument(previous).catch(() => undefined);
      reset(next);
    } catch (error) {
      status = describeApplyError(error);
    }
  }

  function openFixture() {
    opened = null;
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
  }

  function pickProblem(problem: Problem) {
    if (problem.edge) {
      if (problem.block) selected = problem.block;
      selectEdge(problem.edge);
      return;
    }
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
      selectNode(varName);
      return null;
    }
    pinned.delete(varName);
    return addRefusal(outcome.record, form) ?? { field: null, message: outcome.message };
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
      status = VIEWER_ONLY;
      return;
    }
    adder?.openAt(event.clientX, event.clientY, spec);
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
      status = 'the clipboard is not available here';
    }
  }

  async function openEditor(blockVar: string) {
    if (!editable) {
      status = 'opening an editor needs the desktop shell';
      return;
    }
    const span = declarationOf(blockVar);
    if (!span) return;
    try {
      await openInEditor(doc.path, lineOfOffset(doc.source, span.start));
      status = '';
    } catch (error) {
      status = describeApplyError(error);
    }
  }

  function pickInCode(offset: number) {
    const target = targetAt(doc.model.sites, offset);
    if (!target) return;
    siteIndex = target.siteIndex;
    selectNode(target.block);
  }

</script>

<div class="shell">
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
      <button class="primary" onclick={openFile}>Open file…</button>

      <section>
        <h2>File</h2>
        <div class="path" title={doc.path}>{doc.path}</div>
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
          <select bind:value={siteIndex}>
            {#each doc.model.sites as candidate, i (viewIds[i] ?? i)}
              <option value={i}>{siteLabel(candidate)}</option>
            {/each}
          </select>
        </section>
      {/if}

      <Palette
        specs={shownSpecs}
        documentPath={doc.path}
        enabled={editable}
        open={blocksOpen}
        ontoggle={() => (blocksOpen = !blocksOpen)}
        onpick={(spec) => adder?.openAt(window.innerWidth / 2, window.innerHeight / 2, spec)}
      />

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

      <section>
        <h2>Read-only ({notes.length})</h2>
        {#if notes.length === 0}
          <p class="muted">everything in this site is editable</p>
        {:else}
          <ul class="notes">
            {#each notes as note (note.element + note.reason)}
              <li>
                <span class="el">{note.element}</span><span class="reason">{note.reason}</span>
              </li>
            {/each}
          </ul>
        {/if}
      </section>

      <section class="spacer">
        <h2>Example</h2>
        <select bind:value={fixtureName} onchange={openFixture}>
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

    <div class="canvas">
      {#key viewKey}
        <SvelteFlow
          bind:nodes
          bind:edges
          {nodeTypes}
          {edgeTypes}
          colorMode="dark"
          fitView
          fitViewOptions={{ padding: 0.15 }}
          nodesConnectable={editable}
          elementsSelectable={true}
          deleteKey={null}
          minZoom={0.1}
          proOptions={{ hideAttribution: false }}
          ondrop={onDrop}
          ondragover={onDragOver}
          onnodeclick={({ node }) => selectNode(node.id)}
          onedgeclick={({ edge }) => selectEdge(edge.id)}
          onpaneclick={clearSelection}
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
            canUndo={doc.canUndo}
            canRedo={doc.canRedo}
            canOpenEditor={editable}
            canEdit={editable}
            selectedNode={selected}
            {selectedEdge}
            {problems}
            edgeAt={edgeInfo}
            onundo={() => void run(undoDocument)}
            onredo={() => void run(redoDocument)}
            onopen={() => void openFile()}
            ontoggleleft={() => (leftOpen = !leftOpen)}
            ontoggleright={() => (rightOpen = !rightOpen)}
            ontoggledrawer={() => (drawerOpen = !drawerOpen)}
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
          <Controls showLock={false} />
          <TypeLegend entries={legend} />
          <MiniMap bgColor="var(--bg-1)" maskColor="var(--scrim)" nodeColor="var(--border-hi)" />
        </SvelteFlow>
      {/key}

      {#if refusal}
        <div class="dialog" role="dialog" aria-modal="true" data-testid="delete-refusal">
          <h2>{refusal.block} cannot be deleted</h2>
          <p>
            Its declaration is still referenced in {refusal.spans.length}
            {refusal.spans.length === 1 ? 'place' : 'places'} outside the flowgraph. Remove those
            references first — the editor will not rewrite code it does not own.
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
          height={drawerHeight}
          onpick={pickInCode}
          ontoggle={() => (drawerOpen = !drawerOpen)}
          onheight={(next) => (drawerHeight = next)}
        />
      {/if}
    </div>
  </main>

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
  />
</div>

<style>
  .shell {
    display: flex;
    height: 100%;
  }
  .sidebar {
    flex: none;
    width: 280px;
    background: var(--bg-1);
    border-right: 1px solid var(--border);
    overflow: hidden;
    display: flex;
    flex-direction: column;
    transition: width 150ms ease;
  }
  .sidebar.collapsed {
    width: 44px;
  }
  .head {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    padding: var(--sp-3);
  }
  .collapsed .head {
    padding: var(--sp-2) 9px;
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
    padding: 2px 0;
    font-size: 15px;
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
    gap: 1px var(--sp-3);
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
    gap: 5px;
  }
  .notes li {
    display: flex;
    flex-direction: column;
    border-left: 2px solid var(--danger-border);
    padding-left: 7px;
  }
  .el {
    font-size: 11px;
  }
  .reason {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--danger-fg);
  }
  .muted {
    margin: 0;
    color: var(--muted);
    font-size: 11px;
  }
  .spacer {
    margin-top: auto;
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
    font-size: 11px;
    letter-spacing: 0.06em;
    color: var(--muted);
  }
  main {
    position: relative;
    flex: 1;
    min-width: 0;
    background: var(--bg-0);
    display: flex;
    flex-direction: column;
  }
  .canvas {
    position: relative;
    flex: 1;
    min-height: 0;
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
    font-size: 13px;
    font-weight: 600;
    color: var(--danger-fg);
    text-transform: none;
    letter-spacing: 0;
  }
  .dialog p {
    margin: 0 0 var(--sp-2);
    font-size: 11.5px;
    color: var(--fg);
  }
  .dialog ul {
    margin: 0;
    padding: 0;
    list-style: none;
    display: flex;
    flex-direction: column;
    gap: 1px;
  }
  .dialog ul button {
    display: flex;
    gap: var(--sp-3);
    width: 100%;
    padding: 2px var(--sp-2);
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
