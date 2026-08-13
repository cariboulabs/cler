<script lang="ts">
  import RailTabs, { type RailTab } from './RailTabs.svelte';
  import { chips, describeCommand, render, type Message, type Proposal } from './assistant';
  import type { AssistantStatus } from './backend';

  type Props = {
    open: boolean;
    status: AssistantStatus | null;
    messages: Message[];
    pending: number | null;
    selection: { label: string; var: string } | null;
    enabled: boolean;
    note: string;
    revision: number;
    ontoggle: () => void;
    ontab: (next: RailTab) => void;
    onask: (question: string) => void;
    onstop: () => void;
    onsignin: () => Promise<string | null>;
    onlogout: () => void;
    onmodel: (model: string) => void;
    onaccept: (id: number) => void;
    onreject: (id: number) => void;
    onreplan: (id: number) => void;
  };

  const {
    open,
    status,
    messages,
    pending,
    selection,
    enabled,
    note,
    revision,
    ontoggle,
    ontab,
    onask,
    onstop,
    onsignin,
    onlogout,
    onmodel,
    onaccept,
    onreject,
    onreplan
  }: Props = $props();

  let waiting = $state(false);
  let oauthError = $state<string | null>(null);

  const MODELS = [
    { id: 'claude-opus-5', label: 'Opus 5' },
    { id: 'claude-sonnet-5', label: 'Sonnet 5' },
    { id: 'claude-haiku-4-5-20251001', label: 'Haiku 4.5' }
  ];

  const TRANSIENT = 'this conversation is not saved — it goes when the file closes';
  const STALE = 'the graph moved on since this was checked — re-check it before applying';
  const CEILING = 'one proposal per answer: further tool calls in that turn were dropped';

  function stale(proposal: Proposal): boolean {
    return proposal.state === 'ready' && proposal.baseRevision !== revision;
  }

  let draft = $state('');
  let list = $state<HTMLDivElement | null>(null);

  const streaming = $derived(pending !== null);
  const ready = $derived(enabled && status?.available === true);
  const starters = $derived(chips(selection));
  const canSend = $derived(ready && !streaming && draft.trim().length > 0);
  const hint = $derived(status === null ? 'looking for an API key…' : enabled ? '' : note);
  const showRecord = $derived(messages.length > 0 || status?.available === true);
  const showComposer = $derived(status === null || status.available);

  $effect(() => {
    void messages;
    if (list) list.scrollTop = list.scrollHeight;
  });

  function send(question: string) {
    if (!ready || streaming) return;
    const text = question.trim();
    if (text.length === 0) return;
    draft = '';
    onask(text);
  }

  function onKeydown(event: KeyboardEvent) {
    if (event.key !== 'Enter' || event.shiftKey || event.isComposing) return;
    event.preventDefault();
    send(draft);
  }
</script>

<aside class="panel" class:collapsed={!open} data-testid="assistant-panel">
  <div class="head">
    <button
      class="toggle"
      data-testid="toggle-assistant"
      aria-expanded={open}
      aria-label={open ? 'Collapse assistant' : 'Expand assistant'}
      title={open ? 'Collapse assistant  Ctrl+J' : 'Expand assistant  Ctrl+J'}
      onclick={ontoggle}
    >
      {#if open}
        ›
      {:else}
        <svg viewBox="0 0 16 16" width="15" height="15" fill="none" aria-hidden="true">
          <path
            d="M2.5 3.4h11a.9.9 0 0 1 .9.9v6a.9.9 0 0 1-.9.9H6.8L3.4 13.6v-2.4h-.9a.9.9 0 0 1-.9-.9v-6a.9.9 0 0 1 .9-.9Z"
            stroke="currentColor"
            stroke-width="1.4"
            stroke-linejoin="round"
          />
        </svg>
      {/if}
    </button>
    <RailTabs tab="assistant" {ontab} />
  </div>

  <div class="body">
    {#if status?.available === true}
      <div class="model" data-testid="assistant-model">
        <span class="name">
          <select
            class="model-select"
            data-testid="assistant-model-select"
            value={status.model}
            onchange={(event) => onmodel(event.currentTarget.value)}
          >
            {#each MODELS as choice (choice.id)}
              <option value={choice.id}>{choice.label}</option>
            {/each}
            {#if !MODELS.some((choice) => choice.id === status.model)}
              <option value={status.model}>{status.model}</option>
            {/if}
          </select>
          {#if status.method === 'oauth'}
            <button class="linkish signout" data-testid="assistant-logout" onclick={onlogout}>
              Sign out
            </button>
          {/if}
        </span>
      </div>
    {/if}

    {#if status !== null && !status.available}
      <div class="setup" data-testid="assistant-setup">
        <button
          class="primary"
          data-testid="assistant-signin"
          onclick={() => {
            oauthError = null;
            void onsignin().then((error) => {
              oauthError = error;
              waiting = error === null;
            });
          }}
        >
          {waiting ? 'waiting for the browser…' : 'Sign in with Claude'}
        </button>
        {#if oauthError}
          <p class="key-error" data-testid="assistant-oauth-error">{oauthError}</p>
        {/if}
      </div>
    {/if}

    {#if showRecord}
      <div class="list" data-testid="assistant-messages" bind:this={list}>
        {#each messages as message (message.id)}
          {@const lines = render(message.text)}
          <div class="turn {message.role}" data-testid="assistant-message" data-role={message.role}>
            <div class="bubble">
              {#each lines as line, at (at)}
                <div class="line {line.kind}">
                  {#if line.marker}<span class="marker">{line.marker}</span>{/if}
                  <span class="text"
                    >{#each line.pieces as piece, index (index)}{#if piece.code}<code
                          >{piece.text}</code
                        >{:else if piece.strong}<strong>{piece.text}</strong>{:else}{piece.text}{/if}{/each}{#if pending === message.id && at === lines.length - 1}<span
                        class="caret"
                        data-testid="assistant-caret"
                        aria-hidden="true"></span>{/if}</span
                  >
                </div>
              {/each}
            </div>
            {#if message.error}
              <p class="failed" data-testid="assistant-error">{message.error}</p>
            {/if}
            {#if message.proposal}
              {@const plan = message.proposal}
              <div class="plan" data-testid="assistant-proposal" data-state={plan.state}>
                <p class="rationale" data-testid="proposal-rationale">{plan.rationale}</p>
                <ul class="commands" data-testid="proposal-commands">
                  {#each plan.commands as command, at (at)}
                    <li data-testid="proposal-command">{describeCommand(command)}</li>
                  {/each}
                </ul>

                {#if plan.refusal}
                  <p class="refusal" data-testid="proposal-refusal">{plan.refusal}</p>
                {:else if plan.diff.length === 0}
                  <p class="faint" data-testid="proposal-empty">this changes nothing in the file</p>
                {:else}
                  <pre class="diff" data-testid="proposal-diff">{#each plan.diff as line, at (at)}<span
                        class="row {line.kind}">{line.text}
</span>{/each}</pre>
                {/if}

                {#if plan.dropped > 0}
                  <p class="faint" data-testid="proposal-dropped">{CEILING}</p>
                {/if}

                {#if plan.state === 'accepted'}
                  <p class="settled" data-testid="proposal-applied">
                    applied at revision {plan.appliedAt}
                  </p>
                {:else if plan.state === 'rejected'}
                  <p class="settled" data-testid="proposal-rejected">rejected — nothing was applied</p>
                {:else if plan.state === 'refused'}
                  <button data-testid="proposal-reject" onclick={() => onreject(message.id)}>
                    Dismiss
                  </button>
                {:else if stale(plan)}
                  <p class="faint" data-testid="proposal-stale">{STALE}</p>
                  <div class="choose">
                    <button data-testid="proposal-recheck" onclick={() => onreplan(message.id)}>
                      Re-check
                    </button>
                    <button data-testid="proposal-reject" onclick={() => onreject(message.id)}>
                      Reject
                    </button>
                  </div>
                {:else}
                  <div class="choose">
                    <button
                      class="primary"
                      data-testid="proposal-accept"
                      onclick={() => onaccept(message.id)}>Accept</button
                    >
                    <button data-testid="proposal-reject" onclick={() => onreject(message.id)}>
                      Reject
                    </button>
                  </div>
                {/if}
              </div>
            {/if}
            {#if message.usage}
              <p class="usage" data-testid="assistant-usage">
                {message.usage.input_tokens} tokens in · {message.usage.output_tokens} out
              </p>
            {/if}
          </div>
        {/each}

        {#if messages.length === 0}
          <p class="faint" data-testid="assistant-empty">{TRANSIENT}</p>
          <div class="chips">
            {#each starters as chip, index (chip.label)}
              <button
                class="chip"
                data-testid="assistant-chip"
                data-chip={index}
                disabled={!ready || !chip.enabled}
                title={chip.enabled ? chip.label : chip.hint}
                onclick={() => send(chip.question)}>{chip.label}</button
              >
            {/each}
          </div>
        {/if}
      </div>
    {/if}

    {#if showComposer}
      <div class="composer">
        {#if hint}
          <p class="faint" data-testid="assistant-hint">{hint}</p>
        {/if}
        <textarea
          data-testid="assistant-input"
          rows="3"
          placeholder="Ask about this flowgraph…"
          disabled={!ready}
          bind:value={draft}
          onkeydown={onKeydown}
        ></textarea>
        <div class="row">
          <span class="faint">Enter sends · Shift+Enter newline</span>
          {#if streaming}
            <button class="stop" data-testid="assistant-stop" onclick={onstop}>Stop</button>
          {:else}
            <button
              class="primary send"
              data-testid="assistant-send"
              disabled={!canSend}
              onclick={() => send(draft)}>Ask</button
            >
          {/if}
        </div>
      </div>
    {/if}
  </div>
</aside>

<style>
  .panel {
    position: absolute;
    z-index: 8;
    top: calc(var(--bar-h) + var(--sp-3));
    right: var(--sp-3);
    bottom: var(--sp-3);
    width: var(--rail-right);
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
  .toggle {
    flex: none;
    width: 26px;
    padding: var(--sp-0) 0;
    font-size: 14px;
    line-height: 1.1;
    color: var(--muted);
  }
  .toggle svg {
    display: block;
    margin: 0 auto;
  }
  .collapsed :global(.tabs) {
    display: none;
  }
  .body {
    flex: 1;
    min-height: 0;
    width: 320px;
    padding: 0 var(--sp-3) var(--sp-3);
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
    transition:
      opacity 120ms ease,
      visibility 150ms;
  }
  .collapsed .body {
    opacity: 0;
    visibility: hidden;
  }
  .model {
    display: flex;
    flex-direction: column;
    gap: var(--sp-0);
    padding-bottom: var(--sp-2);
    border-bottom: 1px solid var(--border);
  }
  .name {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--fg);
  }
  .faint {
    margin: 0;
    font-size: 11px;
    color: var(--faint);
  }
  .setup {
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
    padding: var(--sp-3);
    border: 1px solid var(--border);
    background: var(--bg-2);
    border-radius: var(--radius-sm);
  }
  .linkish {
    align-self: flex-start;
    padding: 0;
    border: none;
    background: none;
    font-size: 11px;
    color: var(--muted);
    text-decoration: underline;
    cursor: pointer;
  }
  .signout {
    margin-left: var(--sp-2);
  }
  .model-select {
    background: var(--bg-2);
    color: var(--fg);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    font: inherit;
    padding: 1px var(--sp-1);
  }
  .key-error {
    color: var(--danger);
    font-size: 11px;
    margin: 0 0 var(--sp-2);
  }
  .setup p {
    margin: 0;
    font-size: 11px;
    color: var(--fg);
  }
  .setup p.faint {
    color: var(--faint);
  }
  .list {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: var(--sp-3);
  }
  .turn {
    display: flex;
    flex-direction: column;
    gap: var(--sp-0);
  }
  .turn.user {
    align-items: flex-end;
  }
  .bubble {
    max-width: 100%;
    font-size: 12px;
    color: var(--fg);
    word-break: break-word;
  }
  .turn.user .bubble {
    padding: var(--sp-1) var(--sp-2);
    background: var(--bg-2);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    max-width: 85%;
  }
  .line {
    display: flex;
    gap: var(--sp-1);
    min-height: 6px;
  }
  .line.bullet,
  .line.number {
    padding-left: var(--sp-1);
  }
  .marker {
    flex: none;
    color: var(--muted);
    font-family: var(--mono);
    font-size: 11px;
  }
  .text {
    min-width: 0;
  }
  code {
    font-family: var(--mono);
    font-size: 11px;
    padding: 0 var(--sp-0);
    background: var(--bg-2);
    border-radius: var(--radius-xs);
  }
  .caret {
    display: inline-block;
    width: 6px;
    height: 12px;
    margin-left: 2px;
    vertical-align: text-bottom;
    background: var(--accent-hi);
    animation: blink 1s steps(2, start) infinite;
  }
  .failed {
    margin: 0;
    font-size: 11px;
    color: var(--danger-fg);
  }
  .plan {
    margin-top: var(--sp-1);
    padding: var(--sp-2);
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
    background: var(--bg-2);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
  }
  .plan[data-state='accepted'] {
    border-color: var(--ok);
  }
  .plan[data-state='refused'] {
    border-color: var(--danger-border);
  }
  .rationale {
    margin: 0;
    font-size: 12px;
    color: var(--fg);
  }
  .commands {
    margin: 0;
    padding-left: var(--sp-3);
    display: flex;
    flex-direction: column;
    gap: var(--sp-0);
    font-size: 11px;
    color: var(--muted);
  }
  .refusal {
    margin: 0;
    padding: var(--sp-1) var(--sp-2);
    font-size: 11px;
    color: var(--danger-fg);
    background: var(--danger-bg);
    border: 1px solid var(--danger-border);
    border-radius: var(--radius-xs);
  }
  .diff {
    margin: 0;
    max-height: 220px;
    overflow: auto;
    font-family: var(--mono);
    font-size: 11px;
    line-height: 1.45;
    background: var(--bg-1);
    border: 1px solid var(--border);
    border-radius: var(--radius-xs);
    color: var(--muted);
    white-space: pre;
  }
  .row {
    display: block;
    padding: 0 var(--sp-1);
  }
  .row.add {
    color: var(--ok);
    background: color-mix(in srgb, var(--ok) 14%, transparent);
  }
  .row.del {
    color: var(--danger-fg);
    background: color-mix(in srgb, var(--danger) 14%, transparent);
  }
  .row.meta {
    color: var(--faint);
  }
  .choose {
    display: flex;
    gap: var(--sp-2);
  }
  .choose button {
    flex: 1;
    padding: var(--sp-1) var(--sp-2);
    font-size: 11px;
  }
  .settled {
    margin: 0;
    font-size: 11px;
    color: var(--muted);
  }
  .usage {
    margin: 0;
    font-size: 11px;
    color: var(--faint);
  }
  .chips {
    display: flex;
    flex-direction: column;
    gap: var(--sp-1);
    align-items: flex-start;
  }
  .chip {
    max-width: 100%;
    padding: var(--sp-0) var(--sp-2);
    font-size: 11px;
    text-align: left;
    color: var(--muted);
  }
  .composer {
    flex: none;
    display: flex;
    flex-direction: column;
    gap: var(--sp-1);
  }
  textarea {
    width: 100%;
    resize: vertical;
    background: var(--bg-2);
    color: var(--fg);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: var(--sp-1) var(--sp-2);
    font-family: inherit;
    font-size: 12px;
  }
  textarea:focus {
    outline: none;
    border-color: var(--accent-hi);
    box-shadow: var(--glow) color-mix(in srgb, var(--accent) 45%, transparent);
  }
  textarea:disabled {
    color: var(--muted);
    background: var(--bg-1);
    border-style: dashed;
    cursor: not-allowed;
  }
  .row {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
  }
  .row button {
    margin-left: auto;
    flex: none;
    width: auto;
    padding: var(--sp-1) var(--sp-3);
  }
  @keyframes blink {
    50% {
      opacity: 0;
    }
  }
  @media (prefers-reduced-motion: reduce) {
    .panel,
    .body {
      transition: none;
    }
    .caret {
      animation: none;
    }
  }
</style>
