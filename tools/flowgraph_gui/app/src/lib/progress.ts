// The phases a browser build really goes through, as events rather than prose: emception.ts
// and wasmbridge.ts call phase() at the points where the pipeline actually moves on, and the
// progress panel maps them to a label and a fraction. Nothing here rotates on a timer — a
// phrase is on screen only while its phase runs.
export type Phase =
  | 'toolchain'
  | 'boot'
  | 'stage'
  | 'compile'
  | 'link'
  | 'optimize'
  | 'store'
  | 'launch';

export type PhaseEvent = { phase: Phase; detail?: string; bytes?: number; total?: number };

export type Timings = Partial<Record<Phase, number>>;

// Rough shares of a cold build, used only until a real run replaces them (TIMINGS_KEY).
const WEIGHT: Record<Phase, number> = {
  toolchain: 30,
  boot: 6,
  stage: 6,
  compile: 30,
  link: 24,
  optimize: 6,
  store: 1,
  launch: 1
};

const ORDER: Phase[] = ['toolchain', 'boot', 'stage', 'compile', 'link', 'optimize', 'store', 'launch'];

export const TIMINGS_KEY = 'cler.build.timings';

export function label(event: PhaseEvent): string {
  switch (event.phase) {
    case 'toolchain':
      return event.total
        ? `Downloading the compiler… ${mb(event.bytes ?? 0)} / ${mb(event.total)} MB`
        : `Downloading the compiler… ${mb(event.bytes ?? 0)} MB`;
    case 'boot':
      return 'Unpacking clang — warming the toolchain…';
    case 'stage':
      return event.detail ? `Staging cler headers — ${event.detail}…` : 'Staging cler headers…';
    case 'compile':
      return `Compiling ${event.detail ?? 'the flowgraph'}…`;
    case 'link':
      return 'Persuading wasm-ld — linking against libcler_web…';
    case 'optimize':
      return 'Optimizing wasm (Asyncify)…';
    case 'store':
      return 'Storing the build in your browser…';
    case 'launch':
      return 'Wiring runners — launching…';
  }
}

function mb(bytes: number): string {
  return (bytes / 1e6).toFixed(1);
}

function weights(timings: Timings): Record<Phase, number> {
  const out = { ...WEIGHT };
  for (const phase of ORDER) {
    const seen = timings[phase];
    if (seen && seen > 0) out[phase] = seen;
  }
  return out;
}

// Fraction of the whole job, or null when we have no honest number for the phase in flight
// (first ever build, no download bytes) — the bar shimmers instead of guessing.
export function progress(event: PhaseEvent, elapsedInPhase: number, timings: Timings): number | null {
  const w = weights(timings);
  const total = ORDER.reduce((sum, phase) => sum + w[phase], 0);
  const before = ORDER.slice(0, ORDER.indexOf(event.phase)).reduce((sum, phase) => sum + w[phase], 0);
  const within = share(event, elapsedInPhase, timings);
  if (within === null) return null;
  return Math.min(1, (before + within * w[event.phase]) / total);
}

function share(event: PhaseEvent, elapsedInPhase: number, timings: Timings): number | null {
  if (event.phase === 'toolchain' && event.total) return Math.min(1, (event.bytes ?? 0) / event.total);
  const expected = timings[event.phase];
  if (!expected || expected <= 0) return null;
  return Math.min(0.95, elapsedInPhase / expected);
}

// Seconds left, once a previous run told us how long the remaining phases take.
export function remaining(event: PhaseEvent, elapsedInPhase: number, timings: Timings): number | null {
  if (Object.keys(timings).length === 0) return null;
  const w = weights(timings);
  const rest = ORDER.slice(ORDER.indexOf(event.phase) + 1).reduce((sum, phase) => sum + (timings[phase] ?? 0), 0);
  const here = timings[event.phase]
    ? Math.max(0, timings[event.phase]! - elapsedInPhase)
    : event.phase === 'toolchain' && event.total
      ? (1 - Math.min(1, (event.bytes ?? 0) / event.total)) * w.toolchain
      : 0;
  return Math.round((rest + here) / 1000);
}

export const FACTS = [
  'This build is running inside your tab — there is no server compiling anything.',
  'cler-fg edits the .cpp itself: the file is the only source of truth, no project file, no export step.',
  'read_dbf/write_dbf hand a block a pointer into the channel — the default path is zero-copy.',
  'A block owns its input channels; upstream blocks only get a reference to write into.',
  'The progress contract: return cler::Empty{} only when you actually moved a sample.',
  'A block that fails on one input may still succeed on another — errors are retryable, not fatal.',
  'PinnedIslands cuts the graph into contiguous islands, one pinned worker each — the pick for 2 cores.',
  'The core is header-only, so the same flowgraph compiles down to bare metal.',
  'Prefer read_dbf/write_dbf over push/pop: push/pop in a hot path is the classic cler mistake.',
  'The run window is cross-origin isolated, which is the only reason pthreads work in it.'
];

// Tiny bus: the two producers (emception, wasmbridge) are module singletons already, and the
// panel is the one consumer. ponytail: one listener; make it a Set if a second surface wants it.
let listener: ((event: PhaseEvent) => void) | null = null;

export function onPhase(fn: (event: PhaseEvent) => void): () => void {
  listener = fn;
  return () => {
    if (listener === fn) listener = null;
  };
}

export function phase(event: PhaseEvent): void {
  listener?.(event);
}
