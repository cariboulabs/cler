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
  import BlockNode from './lib/BlockNode.svelte';
  import RoutedEdge from './lib/RoutedEdge.svelte';
  import { inTauri, loadFixture, parseFile, pickFile } from './lib/backend';
  import { layout } from './lib/layout';
  import {
    projectSite,
    readOnlyNotes,
    type BlockNode as BlockNodeType,
    type RoutedEdge as RoutedEdgeType
  } from './lib/project';
  import { siteId, siteLabel, type ParseResult, type Site } from './lib/schema';
  import { fixtureNames } from './fixtures';

  const nodeTypes: NodeTypes = { block: BlockNode as unknown as NodeTypes[string] };
  const edgeTypes: EdgeTypes = { routed: RoutedEdge as unknown as EdgeTypes[string] };

  function initialFixture(): string {
    const requested = new URLSearchParams(window.location.search).get('fixture');
    return requested && fixtureNames.includes(requested) ? requested : 'hello_world';
  }

  let fixtureName = $state(initialFixture());
  let result = $state.raw<ParseResult>(loadFixture(fixtureName));
  let siteIndex = $state(0);
  let nodes = $state.raw<BlockNodeType[]>([]);
  let edges = $state.raw<RoutedEdgeType[]>([]);
  let status = $state('');
  let busy = $state(false);

  let viewKey = $state('');

  const site = $derived<Site | undefined>(result.sites[siteIndex]);
  const notes = $derived(site ? readOnlyNotes(site) : []);

  $effect(() => {
    const current = site;
    if (!current) {
      nodes = [];
      edges = [];
      viewKey = '';
      return;
    }
    const key = `${result.file}#${siteId(current)}`;
    let stale = false;
    layout(projectSite(current)).then((laid) => {
      if (stale) return;
      nodes = laid.nodes;
      edges = laid.edges;
      viewKey = key;
    });
    return () => {
      stale = true;
    };
  });

  function show(next: ParseResult) {
    result = next;
    siteIndex = 0;
    status = next.sites.length === 0 ? 'no flowgraph site found in this file' : '';
  }

  async function openFile() {
    if (!inTauri()) {
      status = 'file dialog needs the Tauri shell — pick a fixture below';
      return;
    }
    busy = true;
    status = '';
    try {
      const path = await pickFile();
      if (path) show(await parseFile(path));
    } catch (error) {
      status = String(error);
    } finally {
      busy = false;
    }
  }

  function openFixture() {
    show(loadFixture(fixtureName));
  }
</script>

<div class="shell">
  <aside>
    <div class="toolbar">
      <button onclick={openFile} disabled={busy}>Open file…</button>
    </div>

    <section>
      <h2>File</h2>
      <div class="path" title={result.file}>{result.file}</div>
      <dl>
        <dt>sites</dt>
        <dd>{result.sites.length}</dd>
        <dt>schema</dt>
        <dd>{result.version}</dd>
      </dl>
    </section>

    {#if result.sites.length > 1}
      <section>
        <h2>Site</h2>
        <select bind:value={siteIndex}>
          {#each result.sites as candidate, i (siteId(candidate))}
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
        <ul>
          {#each notes as note (note.element + note.reason)}
            <li><span class="el">{note.element}</span><span class="reason">{note.reason}</span></li>
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
      <p class="status">{status}</p>
    {/if}
  </aside>

  <main>
    {#key viewKey}
      <SvelteFlow
        bind:nodes
        bind:edges
        {nodeTypes}
        {edgeTypes}
        colorMode="light"
        fitView
        fitViewOptions={{ padding: 0.15 }}
        nodesConnectable={false}
        elementsSelectable={true}
        deleteKey={null}
        minZoom={0.1}
        proOptions={{ hideAttribution: false }}
      >
        <Background bgColor="var(--canvas)" patternColor="var(--line)" gap={18} size={2} />
        <Controls showLock={false} />
        <MiniMap bgColor="var(--canvas)" maskColor="var(--scrim)" nodeColor="var(--muted)" />
      </SvelteFlow>
    {/key}
  </main>
</div>

<style>
  .shell {
    display: grid;
    grid-template-columns: 280px 1fr;
    height: 100%;
  }
  aside {
    background: var(--surface);
    border-right: 1px solid var(--line);
    padding: 12px;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 14px;
  }
  .toolbar button {
    width: 100%;
    padding: 7px 10px;
    background: var(--accent);
    color: var(--cler-white);
    border: 1px solid var(--accent);
    border-radius: var(--radius);
    cursor: pointer;
    font: inherit;
    font-weight: 600;
  }
  .toolbar button:hover:not(:disabled) {
    background: var(--accent-dark);
    border-color: var(--accent-dark);
  }
  .toolbar button:disabled {
    background: var(--muted);
    border-color: var(--muted);
    cursor: default;
  }
  h2 {
    margin: 0 0 6px;
    font-size: 10px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  .path {
    font-family: var(--mono);
    font-size: 10.5px;
    color: var(--accent);
    word-break: break-all;
  }
  dl {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 1px 10px;
    margin: 6px 0 0;
  }
  dt {
    color: var(--muted);
  }
  dd {
    margin: 0;
    font-family: var(--mono);
    font-size: 11px;
  }
  select {
    width: 100%;
    background: var(--canvas);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--radius);
    padding: 5px;
    font: inherit;
  }
  select:hover {
    border-color: var(--accent);
  }
  ul {
    margin: 0;
    padding: 0;
    list-style: none;
    display: flex;
    flex-direction: column;
    gap: 5px;
  }
  li {
    display: flex;
    flex-direction: column;
    border-left: 2px solid var(--accent);
    padding-left: 7px;
  }
  .el {
    font-size: 11px;
  }
  .reason {
    font-family: var(--mono);
    font-size: 10px;
    color: var(--accent-dark);
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
    font-size: 11px;
    color: var(--accent-dark);
  }
  main {
    position: relative;
    min-width: 0;
    background: var(--canvas);
  }
</style>
