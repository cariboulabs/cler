<script lang="ts">
  import { untrack } from 'svelte';
  import { FACTS, label, onPhase, progress, remaining, TIMINGS_KEY, type PhaseEvent, type Timings } from './progress';

  type Props = {
    kind: 'check' | 'build' | 'run' | null;
    error: string | null;
    bottom: number;
    onstop: () => void;
    ondiagnostics: () => void;
    ondismiss: () => void;
  };

  const { kind, error, bottom, onstop, ondiagnostics, ondismiss }: Props = $props();

  const FACT_MS = 6000;

  let event = $state.raw<PhaseEvent | null>(null);
  let phaseStart = $state(0);
  let started = $state(0);
  let now = $state(0);
  let fact = $state(0);
  let timings = $state.raw<Timings>(stored());
  const seen: Timings = {};

  function stored(): Timings {
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
        seen[current.phase] = (seen[current.phase] ?? 0) + (at - untrack(() => phaseStart));
        phaseStart = at;
      } else if (!current) {
        phaseStart = at;
      }
      event = next;
    })
  );

  // A new job restarts the clock; a finished one banks what each phase actually cost, so the
  // next build can promise a time instead of shimmering.
  $effect(() => {
    const job = kind;
    untrack(() => settle(job));
  });

  function settle(job: 'check' | 'build' | 'run' | null) {
    if (job === null) {
      if (event && !error) {
        seen[event.phase] = (seen[event.phase] ?? 0) + (performance.now() - phaseStart);
        const merged = { ...timings, ...seen };
        timings = merged;
        localStorage.setItem(TIMINGS_KEY, JSON.stringify(merged));
      }
      event = null;
      return;
    }
    started = performance.now();
    phaseStart = started;
    now = started;
    fact = Math.floor(Math.random() * FACTS.length);
    for (const key of Object.keys(seen)) delete seen[key as keyof Timings];
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

  const cold = $derived(Object.keys(timings).length === 0);
  const elapsed = $derived(Math.max(0, Math.round((now - started) / 1000)));
  const fraction = $derived(event && kind !== 'run' ? progress(event, now - phaseStart, timings) : null);
  const left = $derived(event && kind !== 'run' ? remaining(event, now - phaseStart, timings) : null);
  const status = $derived(
    error
      ? error
      : kind === 'run'
        ? 'Running in a new window — close it or press Stop.'
        : event
          ? label(event)
          : kind === 'check'
            ? 'Starting the in-browser toolchain…'
            : 'Starting…'
  );
</script>

<section class="progress" class:failed={!!error} data-testid="build-progress" style="bottom: {bottom}px">
  <div class="row">
    <span class="what" data-testid="progress-phase" data-phase={error ? 'error' : (event?.phase ?? kind)}>
      {status}
    </span>
    <span class="grow"></span>
    {#if kind === 'run'}
      <button class="chip" data-testid="progress-stop" onclick={onstop}>Stop</button>
    {/if}
    {#if error}
      <button class="chip" data-testid="progress-diagnostics" onclick={ondiagnostics}>see Diagnostics</button>
      <button class="chip" data-testid="progress-dismiss" onclick={ondismiss}>Dismiss</button>
    {:else}
      <span class="time" data-testid="progress-elapsed">
        {elapsed}s{#if left !== null} · ~{left}s left{/if}
      </span>
    {/if}
  </div>
  {#if !error}
    <div
      class="bar"
      class:indeterminate={fraction === null}
      data-testid="progress-bar"
      role="progressbar"
      aria-valuemin="0"
      aria-valuemax="100"
      aria-valuenow={fraction === null ? undefined : Math.round(fraction * 100)}
    >
      <div class="fill" style={fraction === null ? '' : `width: ${(fraction * 100).toFixed(1)}%`}></div>
    </div>
    {#if cold && kind !== 'run'}
      <span class="note" data-testid="progress-cold">
        first build in this browser also fetches the 25 MB compiler — about 30 s longer, once.
      </span>
    {/if}
    {#key fact}
      <span class="fact" data-testid="progress-fact">{FACTS[fact]}</span>
    {/key}
  {/if}
</section>

<style>
  .progress {
    position: absolute;
    left: calc(var(--rail-left) + 2 * var(--sp-3));
    right: calc(var(--rail-right) + 2 * var(--sp-3));
    z-index: 7;
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
    padding: var(--sp-3);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
  }
  .progress.failed {
    border-color: var(--danger-border);
    background: color-mix(in srgb, var(--danger-bg) 78%, transparent);
  }
  .row {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
  }
  .what {
    font-size: 13px;
    color: var(--fg);
  }
  .failed .what {
    color: var(--danger-fg);
    font-family: var(--mono);
    font-size: 12px;
  }
  .grow {
    flex: 1;
  }
  .time {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .chip {
    flex: none;
    width: auto;
    padding: 0 var(--sp-2);
    font-size: 11px;
  }
  .bar {
    position: relative;
    height: 4px;
    overflow: hidden;
    background: var(--bg-2);
    border-radius: var(--radius-xs);
  }
  .fill {
    height: 100%;
    width: 0;
    background: var(--accent-hi);
    box-shadow: var(--glow) var(--accent);
    transition: width 300ms ease;
  }
  .bar.indeterminate .fill {
    width: 32%;
    animation: shimmer 1.6s ease-in-out infinite;
  }
  @keyframes shimmer {
    0% {
      transform: translateX(-110%);
    }
    100% {
      transform: translateX(330%);
    }
  }
  .note {
    font-size: 11px;
    color: var(--warn);
  }
  .fact {
    font-size: 11px;
    color: var(--muted);
    animation: fade 600ms ease;
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
    .bar.indeterminate .fill,
    .fact {
      animation: none;
    }
  }
</style>
