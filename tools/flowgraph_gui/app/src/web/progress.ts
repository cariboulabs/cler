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

const COLD_BUILD_WEIGHT: Record<Phase, number> = {
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

export function parts(event: PhaseEvent): { phrase: string; target: string | null } {
  switch (event.phase) {
    case 'toolchain':
      return {
        phrase: 'Downloading the compiler…',
        target: event.total
          ? `${mb(event.bytes ?? 0)} / ${mb(event.total)} MB`
          : `${mb(event.bytes ?? 0)} MB`
      };
    case 'boot':
      return { phrase: 'Unpacking clang — warming the toolchain…', target: null };
    case 'stage':
      return { phrase: event.detail ? 'Staging cler headers' : 'Staging cler headers…', target: event.detail ?? null };
    case 'compile':
      return { phrase: 'Compiling', target: event.detail ?? 'the flowgraph' };
    case 'link':
      return { phrase: 'Persuading wasm-ld — linking against libcler_web…', target: null };
    case 'optimize':
      return { phrase: 'Optimizing wasm (Asyncify)…', target: null };
    case 'store':
      return { phrase: 'Storing the build in your browser…', target: null };
    case 'launch':
      return { phrase: 'Wiring runners — launching…', target: null };
  }
}

function mb(bytes: number): string {
  return (bytes / 1e6).toFixed(1);
}

function effectiveWeights(timings: Timings): Record<Phase, number> {
  const out = { ...COLD_BUILD_WEIGHT };
  for (const phase of ORDER) {
    const seen = timings[phase];
    if (seen && seen > 0) out[phase] = seen;
  }
  return out;
}

export function progress(event: PhaseEvent, elapsedInPhase: number, timings: Timings): number | null {
  const w = effectiveWeights(timings);
  const total = ORDER.reduce((sum, phase) => sum + w[phase], 0);
  const before = ORDER.slice(0, ORDER.indexOf(event.phase)).reduce((sum, phase) => sum + w[phase], 0);
  const within = phaseShare(event, elapsedInPhase, timings);
  if (within === null) return null;
  return Math.min(1, (before + within * w[event.phase]) / total);
}

function phaseShare(event: PhaseEvent, elapsedInPhase: number, timings: Timings): number | null {
  if (event.phase === 'toolchain' && event.total) return Math.min(1, (event.bytes ?? 0) / event.total);
  const expected = timings[event.phase];
  if (!expected || expected <= 0) return null;
  return Math.min(0.95, elapsedInPhase / expected);
}

export function remaining(event: PhaseEvent, elapsedInPhase: number, timings: Timings): number | null {
  if (Object.keys(timings).length === 0) return null;
  const w = effectiveWeights(timings);
  const rest = ORDER.slice(ORDER.indexOf(event.phase) + 1).reduce((sum, phase) => sum + (timings[phase] ?? 0), 0);
  const here = timings[event.phase]
    ? Math.max(0, timings[event.phase]! - elapsedInPhase)
    : event.phase === 'toolchain' && event.total
      ? (1 - Math.min(1, (event.bytes ?? 0) / event.total)) * w.toolchain
      : 0;
  return Math.round((rest + here) / 1000);
}


// ponytail: one listener — the panel is the only consumer; make it a Set if a second surface wants it.
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
