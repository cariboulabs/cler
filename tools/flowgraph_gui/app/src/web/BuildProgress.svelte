<script module lang="ts">
  export const STRIP_H = 62;
</script>

<script lang="ts">
  import { untrack } from 'svelte';
  import {
    FACTS,
    onPhase,
    parts,
    progress,
    remaining,
    TIMINGS_KEY,
    type PhaseEvent,
    type Timings
  } from './progress';

  type Props = {
    kind: 'check' | 'build' | 'run' | null;
    done: 'check' | 'build' | null;
    error: string | null;
    docked: boolean;
    bottom: number;
    onstop: () => void;
    ondiagnostics: () => void;
    ondismiss: () => void;
    onopen: () => void;
  };

  const { kind, done, error, docked, bottom, onstop, ondiagnostics, ondismiss, onopen }: Props =
    $props();

  const FACT_MS = 6000;

  const TOOLCHAIN_NOTE = 'the first build in this browser also fetches the 25 MB compiler — once.';

  let event = $state.raw<PhaseEvent | null>(null);
  let phaseStartedAt = $state(0);
  let jobStartedAt = $state(0);
  let now = $state(0);
  let fact = $state(0);
  let timings = $state.raw<Timings>(storedTimings());
  const phaseDurations: Timings = {};

  function storedTimings(): Timings {
    try {
      return JSON.parse(localStorage.getItem(TIMINGS_KEY) ?? '{}') as Timings;
    } catch {
      return {};
    }
  }

  $effect(() =>
    onPhase((next) => {
      const at = performance.now();
      const current = untrack(() => event);
      if (current && current.phase !== next.phase) {
        phaseDurations[current.phase] = (phaseDurations[current.phase] ?? 0) + (at - untrack(() => phaseStartedAt));
        phaseStartedAt = at;
      } else if (!current) {
        phaseStartedAt = at;
      }
      event = next;
    })
  );

  $effect(() => {
    const job = kind;
    untrack(() => startOrFinishJob(job));
  });

  function startOrFinishJob(job: 'check' | 'build' | 'run' | null) {
    if (job === null) {
      if (event && !error) {
        phaseDurations[event.phase] = (phaseDurations[event.phase] ?? 0) + (performance.now() - phaseStartedAt);
        const merged = { ...timings, ...phaseDurations };
        timings = merged;
        localStorage.setItem(TIMINGS_KEY, JSON.stringify(merged));
      }
      event = null;
      return;
    }
    jobStartedAt = performance.now();
    phaseStartedAt = jobStartedAt;
    now = jobStartedAt;
    fact = Math.floor(Math.random() * FACTS.length);
    for (const key of Object.keys(phaseDurations)) delete phaseDurations[key as keyof Timings];
  }

  $effect(() => {
    if (kind === null) return;
    const tick = setInterval(() => (now = performance.now()), 250);
    const roll = setInterval(() => (fact = (fact + 1) % FACTS.length), FACT_MS);
    return () => {
      clearInterval(tick);
      clearInterval(roll);
    };
  });

  const elapsed = $derived(Math.max(0, Math.round((now - jobStartedAt) / 1000)));
  const fraction = $derived(event && kind !== 'run' ? progress(event, now - phaseStartedAt, timings) : null);
  const left = $derived(event && kind !== 'run' ? remaining(event, now - phaseStartedAt, timings) : null);
  const shown = $derived(event ? parts(event) : null);

  const phrase = $derived(
    error
      ? error
      : done
        ? done === 'build'
          ? 'Build finished'
          : 'Check finished'
        : kind === 'run'
          ? 'Running in a new window'
          : (shown?.phrase ??
            (kind === 'check' ? 'Starting the in-browser toolchain…' : 'Starting…'))
  );
  const target = $derived(error || done ? null : (shown?.target ?? null));
  const clock = $derived(
    done
      ? elapsed > 0
        ? `${elapsed}s`
        : ''
      : elapsed < 1
        ? ''
        : `${elapsed}s${left !== null ? ` · ~${left}s left` : ''}`
  );
  const note = $derived(event?.phase === 'toolchain' ? TOOLCHAIN_NOTE : null);
</script>

<section
  class="strip"
  class:pill={!docked}
  class:failed={!!error}
  class:done={!!done && !error}
  data-testid="build-progress"
  style="bottom: {bottom}px; {docked ? `height: ${STRIP_H}px` : ''}"
>
  {#if !docked}
    <button
      class="open"
      data-testid="progress-open"
      title="Open the drawer"
      aria-label="Open the drawer"
      onclick={onopen}
    ></button>
  {/if}

  <div class="row">
    <span class="what" data-testid="progress-phase" data-phase={error ? 'error' : done ? 'done' : (event?.phase ?? kind)}>
      {phrase}
    </span>
    {#if target}<code class="target">{target}</code>{/if}
    <span class="grow"></span>
    {#if kind === 'run'}
      <button class="link" data-testid="progress-stop" onclick={onstop}>Stop</button>
    {/if}
    {#if error}
      <button class="link" data-testid="progress-diagnostics" onclick={ondiagnostics}>
        Diagnostics
      </button>
      <button class="link" data-testid="progress-dismiss" onclick={ondismiss}>Dismiss</button>
    {:else}
      <span class="time" data-testid="progress-elapsed">{clock}</span>
    {/if}
  </div>

  <div
    class="track"
    class:indeterminate={!error && !done && fraction === null}
    data-testid="progress-bar"
    role="progressbar"
    aria-valuemin="0"
    aria-valuemax="100"
    aria-valuenow={done ? 100 : fraction === null ? undefined : Math.round(fraction * 100)}
  >
    <div
      class="fill"
      style={done || error ? 'width: 100%' : fraction === null ? '' : `width: ${(fraction * 100).toFixed(1)}%`}
    ></div>
  </div>

  {#if docked && !error}
    <div class="under">
      {#if note}
        <span class="tag">first build</span>
        <span class="fact" data-testid="progress-cold">{note}</span>
      {:else}
        <span class="tag">did you know</span>
        {#key fact}
          <span class="fact" data-testid="progress-fact">{FACTS[fact]}</span>
        {/key}
      {/if}
    </div>
  {/if}
</section>

<style>
  .strip {
    position: absolute;
    left: calc(var(--rail-left) + 2 * var(--sp-3));
    right: calc(var(--rail-right) + 2 * var(--sp-3));
    z-index: 8;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    padding: var(--sp-2) var(--sp-3) 0;
    pointer-events: none;
  }
  .strip :global(button) {
    pointer-events: auto;
  }
  .row {
    display: flex;
    align-items: baseline;
    gap: var(--sp-2);
    min-width: 0;
  }
  .what {
    flex: none;
    font-size: 12px;
    color: var(--fg);
    white-space: nowrap;
  }
  .failed .what {
    min-width: 0;
    flex: 1;
    font-family: var(--mono);
    font-size: 11px;
    color: var(--danger-fg);
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .target {
    min-width: 0;
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .grow {
    flex: 1;
  }
  .failed .grow {
    flex: none;
  }
  .time {
    flex: none;
    font-family: var(--mono);
    font-size: 11px;
    font-variant-numeric: tabular-nums;
    color: var(--faint);
  }
  .link {
    flex: none;
    width: auto;
    padding: 0;
    background: transparent;
    border: 0;
    font-size: 11px;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    font-weight: 600;
    color: var(--danger-fg);
  }
  .link:hover {
    background: transparent;
    color: var(--fg);
    text-decoration: underline;
  }

  .track {
    position: relative;
    flex: none;
    height: 3px;
    margin: var(--sp-2) calc(-1 * var(--sp-3)) 0;
    overflow: hidden;
    background: var(--bg-2);
  }
  .fill {
    height: 100%;
    width: 0;
    background: var(--accent-hi);
    box-shadow: var(--glow) var(--accent);
    transition: width 200ms ease;
  }
  .done .fill {
    background: var(--ok);
    box-shadow: var(--glow) var(--ok);
  }
  .failed .fill {
    background: var(--danger);
    box-shadow: none;
  }
  .track.indeterminate .fill {
    width: 28%;
    background: linear-gradient(
      90deg,
      transparent,
      color-mix(in srgb, var(--accent-hi) 85%, transparent),
      transparent
    );
    box-shadow: none;
    animation: shimmer 1.8s ease-in-out infinite;
  }
  @keyframes shimmer {
    0% {
      transform: translateX(-110%);
    }
    100% {
      transform: translateX(370%);
    }
  }

  .under {
    display: flex;
    align-items: baseline;
    gap: var(--sp-2);
    min-width: 0;
    padding-top: 5px;
  }
  .tag {
    flex: none;
    font-size: 10px;
    letter-spacing: 0.11em;
    text-transform: uppercase;
    font-weight: 600;
    color: var(--faint);
  }
  .fact {
    min-width: 0;
    font-size: 11px;
    color: var(--muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    animation: fade 400ms ease;
  }

  .strip.pill {
    left: 50%;
    right: auto;
    transform: translateX(-50%);
    width: min(460px, 60%);
    padding: var(--sp-1) 0 0;
    pointer-events: auto;
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
  }
  .pill.failed {
    border-color: var(--danger-border);
  }
  .pill .open {
    position: absolute;
    inset: 0;
    padding: 0;
    background: transparent;
    border: 0;
    border-radius: var(--radius);
    cursor: pointer;
  }
  .pill .row {
    padding: 0 var(--sp-3) var(--sp-1);
  }
  .pill .what,
  .pill .target,
  .pill .time {
    font-size: 11px;
  }
  .pill .track {
    margin: 0;
    height: 2px;
  }

  @keyframes fade {
    from {
      opacity: 0;
    }
    to {
      opacity: 1;
    }
  }
  @media (prefers-reduced-motion: reduce) {
    .fill,
    .fact {
      transition: none;
      animation: none;
    }
    .track.indeterminate .fill {
      width: 100%;
      background: color-mix(in srgb, var(--accent-hi) 45%, transparent);
    }
  }
</style>
