import { targetAt, type CodeTarget, type Problem } from './project';
import type { Site, Span } from './schema';

export type Severity = 'error' | 'warning';

export type Diagnostic = {
  file: string;
  line: number;
  column: number;
  severity: Severity;
  message: string;
  notes: string[];
};

export type Placed = Diagnostic & {
  id: string;
  site: number | null;
  block: string | null;
  span: Span | null;
};

const HEAD = /^(.*?):(\d+):(\d+):\s+(fatal error|error|warning|note):\s+(.*)$/;

export function parseDiagnostics(lines: string[]): Diagnostic[] {
  const found: Diagnostic[] = [];
  for (const line of lines) {
    const match = HEAD.exec(line);
    if (!match) continue;
    const [, file, row, column, severity, message] = match;
    if (file === undefined || row === undefined || column === undefined) continue;
    if (severity === undefined || message === undefined) continue;
    if (severity === 'note') {
      found.at(-1)?.notes.push(`${file}:${row}: ${message}`);
      continue;
    }
    found.push({
      file,
      line: Number(row),
      column: Number(column),
      severity: severity === 'warning' ? 'warning' : 'error',
      message,
      notes: []
    });
  }
  return found;
}

export function offsetOfLine(source: string, line: number, column: number): number {
  let at = 0;
  for (let step = 1; step < line; step++) {
    const next = source.indexOf('\n', at);
    if (next === -1) return source.length;
    at = next + 1;
  }
  return Math.min(at + Math.max(column - 1, 0), source.length);
}

function lineSpan(source: string, offset: number): Span {
  const start = source.lastIndexOf('\n', Math.max(offset - 1, 0)) + 1;
  const end = source.indexOf('\n', offset);
  return { start, end: end === -1 ? source.length : end };
}

function samePath(one: string, other: string): boolean {
  return one === other || one.endsWith(`/${other}`) || other.endsWith(`/${one}`);
}

function nearest(sites: Site[], offset: number): CodeTarget | null {
  const siteIndex = sites.findIndex(
    (site) => offset >= site.span.start && offset < site.span.end
  );
  const site = sites[siteIndex];
  if (!site) return null;
  const spots = [
    ...site.blocks.map((block) => ({ block: block.var, span: block.span })),
    ...site.runners.map((runner) => ({ block: runner.block, span: runner.span }))
  ];
  let best: { block: string; away: number } | null = null;
  for (const spot of spots) {
    const away = offset < spot.span.start ? spot.span.start - offset : offset - spot.span.end;
    if (!best || away < best.away) best = { block: spot.block, away };
  }
  return best === null ? null : { siteIndex, block: best.block };
}

export function placeDiagnostics(
  found: Diagnostic[],
  path: string,
  source: string,
  sites: Site[]
): Placed[] {
  return found.map((entry, index) => {
    const id = `diag:${index}`;
    if (!samePath(entry.file, path)) return { ...entry, id, site: null, block: null, span: null };
    const offset = offsetOfLine(source, entry.line, entry.column);
    const owner = targetAt(sites, offset) ?? nearest(sites, offset);
    return {
      ...entry,
      id,
      site: owner?.siteIndex ?? null,
      block: owner?.block ?? null,
      span: lineSpan(source, offset)
    };
  });
}

export function compileProblems(placed: Placed[]): Problem[] {
  return placed
    .filter((entry): entry is Placed & { span: Span } => entry.span !== null)
    .map((entry) => ({
      id: entry.id,
      kind: 'compile',
      severity: entry.severity,
      title: entry.block ?? `line ${entry.line}`,
      detail: entry.message,
      span: entry.span,
      edge: null,
      block: entry.block
    }));
}
