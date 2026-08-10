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
  import Actions from './lib/Actions.svelte';
  import BlockNode from './lib/BlockNode.svelte';
  import Inspector from './lib/Inspector.svelte';
  import RoutedEdge from './lib/RoutedEdge.svelte';
  import {
    applyCommands,
    closeDocument,
    describeApplyError,
    inTauri,
    loadFixture,
    onExternalChange,
    openDocument,
    pickFile,
    queued,
    redoDocument,
    reloadDocument,
    undoDocument
  } from './lib/backend';
  import { layout } from './lib/layout';
  import {
    mergeProjection,
    projectSite,
    readOnlyNotes,
    type BlockNode as BlockNodeType,
    type RoutedEdge as RoutedEdgeType
  } from './lib/project';
  import {
    siteLabel,
    siteViewIds,
    type Command,
    type DocumentState,
    type Site
  } from './lib/schema';
  import { fixtureNames } from './fixtures';
  import type { Outcome } from './lib/inspector';

  const nodeTypes: NodeTypes = { block: BlockNode as unknown as NodeTypes[string] };
  const edgeTypes: EdgeTypes = { routed: RoutedEdge as unknown as EdgeTypes[string] };

  function initialFixture(): string {
    const requested = new URLSearchParams(window.location.search).get('fixture');
    return requested && fixtureNames.includes(requested) ? requested : 'hello_world';
  }

  const editable = inTauri();
  const DISK_DRIFT = 'changed on disk';
  const LEFT_PANEL = 'cler.panel.left';
  const RIGHT_PANEL = 'cler.panel.right';

  function storedOpen(key: string): boolean {
    return localStorage.getItem(key) !== 'closed';
  }

  function storeOpen(key: string, open: boolean): void {
    localStorage.setItem(key, open ? 'open' : 'closed');
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
  let leftOpen = $state(storedOpen(LEFT_PANEL));
  let rightOpen = $state(storedOpen(RIGHT_PANEL));
  let inspector = $state<Inspector | null>(null);
  let generation = 0;

  const site = $derived<Site | undefined>(doc.model.sites[siteIndex]);
  const viewIds = $derived(siteViewIds(doc.model.sites));
  const viewId = $derived(viewIds[siteIndex] ?? '');
  const notes = $derived(site ? readOnlyNotes(site) : []);
  const needsReload = $derived(changedOnDisk || doc.externalChange);

  $effect(() => storeOpen(LEFT_PANEL, leftOpen));
  $effect(() => storeOpen(RIGHT_PANEL, rightOpen));

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
    const fresh = projectSite(current);
    if (untrack(() => viewKey) === key) {
      const merged = mergeProjection(untrack(() => ({ nodes, edges })), fresh);
      nodes = merged.nodes;
      edges = merged.edges;
      return;
    }
    let stale = false;
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
    if (fresh) {
      siteIndex = 0;
      selected = null;
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
      return { ok: false, message };
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

  async function submit(command: Command): Promise<Outcome> {
    if (!editable) return { ok: false, message: 'fixture mode is a read-only viewer' };
    return run((path) => applyCommands(path, [command], doc.revision));
  }

  async function openFile() {
    if (!editable) {
      status = 'file dialog needs the desktop shell — pick a fixture below';
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
    rightOpen = true;
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
        <h2>Fixture</h2>
        <select bind:value={fixtureName} onchange={openFixture}>
          {#each fixtureNames as name (name)}
            <option value={name}>{name}</option>
          {/each}
        </select>
      </section>

      {#if status}
        <p class="status" data-testid="status">{status}</p>
      {/if}

      <span class="attribution">Caribou Labs</span>
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
          nodesConnectable={false}
          elementsSelectable={true}
          deleteKey={null}
          minZoom={0.1}
          proOptions={{ hideAttribution: false }}
          onnodeclick={({ node }) => selectNode(node.id)}
          onpaneclick={() => (selected = null)}
        >
          <Background bgColor="var(--bg-0)" patternColor="var(--border)" gap={18} size={2} />
          <Actions
            canUndo={doc.canUndo}
            canRedo={doc.canRedo}
            onundo={() => void run(undoDocument)}
            onredo={() => void run(redoDocument)}
            onopen={() => void openFile()}
            ontoggleleft={() => (leftOpen = !leftOpen)}
            ontoggleright={() => (rightOpen = !rightOpen)}
          />
          <Controls showLock={false} />
          <MiniMap bgColor="var(--bg-1)" maskColor="var(--scrim)" nodeColor="var(--border-hi)" />
        </SvelteFlow>
      {/key}
    </div>
  </main>

  <Inspector
    bind:this={inspector}
    path={doc.path}
    {site}
    {siteIndex}
    {selected}
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
  @media (prefers-reduced-motion: reduce) {
    .sidebar,
    .body {
      transition: none;
    }
  }
</style>
