import type { Span } from './schema';

export type CodeMark = { span: Span; reason: string };

export type CodePiece = {
  at: number;
  text: string;
  kind: string;
  hit: boolean;
  reason: string | null;
};

export type CodeLine = { number: number; start: number; pieces: CodePiece[] };

type Token = { start: number; end: number; kind: string };

const KEYWORDS = new Set([
  'alignas', 'alignof', 'auto', 'break', 'case', 'catch', 'class', 'concept', 'const',
  'consteval', 'constexpr', 'constinit', 'const_cast', 'continue', 'co_await', 'co_return',
  'co_yield', 'decltype', 'default', 'delete', 'do', 'dynamic_cast', 'else', 'enum', 'explicit',
  'export', 'extern', 'false', 'final', 'for', 'friend', 'goto', 'if', 'inline', 'mutable',
  'namespace', 'new', 'noexcept', 'nullptr', 'operator', 'override', 'private', 'protected',
  'public', 'register', 'reinterpret_cast', 'requires', 'return', 'sizeof', 'static',
  'static_assert', 'static_cast', 'struct', 'switch', 'template', 'this', 'thread_local',
  'throw', 'true', 'try', 'typedef', 'typeid', 'typename', 'union', 'using', 'virtual',
  'volatile', 'while'
]);

const TYPES = new Set([
  'bool', 'char', 'char8_t', 'char16_t', 'char32_t', 'double', 'float', 'int', 'int8_t',
  'int16_t', 'int32_t', 'int64_t', 'long', 'short', 'signed', 'size_t', 'ssize_t', 'uint8_t',
  'uint16_t', 'uint32_t', 'uint64_t', 'unsigned', 'void', 'wchar_t'
]);

const GRAMMAR =
  /(?<cmt>\/\/[^\n]*|\/\*[\s\S]*?\*\/)|(?<pre>^[ \t]*#[^\n]*)|(?<str>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')|(?<lit>\b\d[\w.']*)|(?<word>[A-Za-z_]\w*)/gm;

function wordKind(word: string): string {
  if (KEYWORDS.has(word)) return 'kw';
  if (TYPES.has(word)) return 'typ';
  return '';
}

function matchKind(groups: Record<string, string | undefined>): string {
  if (groups.cmt !== undefined) return 'cmt';
  if (groups.pre !== undefined) return 'pre';
  if (groups.str !== undefined) return 'str';
  if (groups.lit !== undefined) return 'lit';
  return wordKind(groups.word ?? '');
}

export function tokenize(source: string): Token[] {
  const tokens: Token[] = [];
  GRAMMAR.lastIndex = 0;
  for (const found of source.matchAll(GRAMMAR)) {
    const kind = matchKind(found.groups ?? {});
    if (kind === '') continue;
    tokens.push({ start: found.index, end: found.index + found[0].length, kind });
  }
  return tokens;
}

function stopsIn(source: string, spans: Span[], tokens: Token[]): number[] {
  const stops = new Set<number>([0, source.length]);
  for (let at = source.indexOf('\n'); at >= 0; at = source.indexOf('\n', at + 1)) {
    stops.add(at);
    stops.add(at + 1);
  }
  for (const token of tokens) {
    stops.add(token.start);
    stops.add(token.end);
  }
  for (const span of spans) {
    if (span.start >= 0 && span.start <= source.length) stops.add(span.start);
    if (span.end >= 0 && span.end <= source.length) stops.add(span.end);
  }
  return [...stops].sort((a, b) => a - b);
}

function covers(span: Span, at: number): boolean {
  return at >= span.start && at < span.end;
}

export function codeLines(
  source: string,
  hits: Span[],
  marks: CodeMark[],
  anchors: Span[] = []
): CodeLine[] {
  const tokens = tokenize(source);
  const spans = [...hits, ...marks.map((mark) => mark.span), ...anchors];
  const stops = stopsIn(source, spans, tokens);
  const lines: CodeLine[] = [{ number: 1, start: 0, pieces: [] }];
  let cursor = 0;

  for (let index = 0; index + 1 < stops.length; index++) {
    const from = stops[index] ?? 0;
    const to = stops[index + 1] ?? 0;
    const text = source.slice(from, to);
    if (text === '\n') {
      lines.push({ number: lines.length + 1, start: to, pieces: [] });
      continue;
    }
    while (cursor < tokens.length && (tokens[cursor]?.end ?? 0) <= from) cursor++;
    const token = tokens[cursor];
    const line = lines[lines.length - 1];
    line?.pieces.push({
      at: from,
      text,
      kind: token && token.start <= from ? token.kind : '',
      hit: hits.some((span) => covers(span, from)),
      reason: marks.find((mark) => covers(mark.span, from))?.reason ?? null
    });
  }
  return lines;
}
