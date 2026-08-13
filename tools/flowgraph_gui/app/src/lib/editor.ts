import { cpp } from '@codemirror/lang-cpp';
import { HighlightStyle, syntaxHighlighting } from '@codemirror/language';
import { defaultKeymap, history, historyKeymap, indentWithTab } from '@codemirror/commands';
import { Annotation, Compartment, EditorState, StateEffect, StateField } from '@codemirror/state';
import {
  Decoration,
  EditorView,
  drawSelection,
  dropCursor,
  highlightActiveLine,
  highlightActiveLineGutter,
  keymap,
  lineNumbers,
  type DecorationSet
} from '@codemirror/view';
import { tags } from '@lezer/highlight';

import type { Span } from './schema';

export type CodeMark = { span: Span; reason: string };

export type Spans = { hits: Span[]; marks: CodeMark[]; faults: Span[] };

export type EditorHooks = {
  onedit: (source: string) => void;
  onpick: (offset: number) => void;
};

const foreign = Annotation.define<boolean>();
const editable = new Compartment();
const setSpans = StateEffect.define<Spans>();

const highlight = HighlightStyle.define([
  { tag: tags.keyword, color: 'var(--type-5)' },
  { tag: [tags.typeName, tags.standard(tags.typeName)], color: 'var(--type-1)' },
  { tag: [tags.string, tags.character], color: 'var(--type-2)' },
  { tag: [tags.number, tags.bool, tags.null], color: 'var(--type-3)' },
  { tag: [tags.comment, tags.lineComment, tags.blockComment], color: 'var(--faint)', fontStyle: 'italic' },
  { tag: [tags.meta, tags.processingInstruction], color: 'var(--type-3)' },
  { tag: tags.operator, color: 'var(--muted)' }
]);

// Editable code wears the app's field dress (fill, border, focus ring); a read-only
// viewer stays flat on the drawer's glass, so the two states never look alike.
const theme = EditorView.theme({
  '&': { color: 'var(--fg)', backgroundColor: 'transparent', height: '100%' },
  '&.editable': {
    backgroundColor: 'var(--bg-2)',
    border: '1px solid var(--border)',
    borderRadius: 'var(--radius-sm)'
  },
  '&.editable:hover': { borderColor: 'var(--border-hi)' },
  '&.editable.cm-focused': { outline: '2px solid var(--accent-hi)', outlineOffset: '-1px' },
  '&.editable .cm-activeLine': {
    backgroundColor: 'color-mix(in srgb, var(--fg) 6%, transparent)'
  },
  '&.editable .cm-activeLineGutter': { color: 'var(--fg)' },
  '&:not(.editable) .cm-activeLine': { backgroundColor: 'transparent' },
  '.cm-content': {
    fontFamily: 'var(--mono)',
    fontSize: '12px',
    padding: '0',
    caretColor: 'var(--fg)'
  },
  '.cm-line': { lineHeight: '1.5', padding: '0 var(--sp-2)', whiteSpace: 'pre' },
  '.cm-scroller': { fontFamily: 'var(--mono)', lineHeight: '1.5', overflow: 'auto' },
  '.cm-gutters': {
    backgroundColor: 'transparent',
    border: 'none',
    color: 'var(--faint)',
    paddingRight: 'var(--sp-3)'
  },
  '.cm-activeLineGutter': { backgroundColor: 'transparent' },
  '&.cm-focused': { outline: 'none' },
  '.cm-cursor, .cm-dropCursor': { borderLeftColor: 'var(--fg)', borderLeftWidth: '2px' },
  // Selection has to win against CodeMirror's own focused rule, and stay dark
  // enough that the syntax colours on top of it are still readable.
  '.cm-selectionBackground, &.cm-focused > .cm-scroller > .cm-selectionLayer .cm-selectionBackground':
    { backgroundColor: 'color-mix(in srgb, var(--type-1) 28%, var(--bg-0))' },
  '.cm-content ::selection, .cm-content::selection': {
    backgroundColor: 'color-mix(in srgb, var(--type-1) 28%, var(--bg-0))'
  },
  '.hit': {
    backgroundColor: 'color-mix(in srgb, var(--accent) 30%, transparent)',
    borderRadius: 'var(--radius-xs)'
  },
  '.ro': {
    textDecoration: 'underline wavy var(--faint)',
    textUnderlineOffset: '3px'
  },
  '.bad': {
    textDecoration: 'underline wavy var(--danger)',
    textUnderlineOffset: '3px',
    textDecorationThickness: '1.5px'
  }
});

// The class rides the state, not the DOM: CodeMirror rewrites className on update.
function writableExtension(writable: boolean) {
  return [
    EditorView.editable.of(writable),
    EditorView.editorAttributes.of({ class: writable ? 'editable' : '' })
  ];
}

// One decoration per line of a span: CodeMirror renders each as its own element,
// so a test (and a screen reader) sees where every marked run starts.
function perLine(
  state: EditorState,
  span: Span,
  spec: Parameters<typeof Decoration.mark>[0]
): { from: number; to: number; value: Decoration }[] {
  const pieces: { from: number; to: number; value: Decoration }[] = [];
  const start = Math.max(0, Math.min(span.start, state.doc.length));
  const end = Math.max(start, Math.min(span.end, state.doc.length));
  for (let at = start; at < end; ) {
    const line = state.doc.lineAt(at);
    const stop = Math.min(end, line.to);
    if (stop > at) {
      pieces.push({
        from: at,
        to: stop,
        value: Decoration.mark({ ...spec, attributes: { ...spec.attributes, 'data-at': String(at) } })
      });
    }
    at = stop + 1;
  }
  return pieces;
}

// A fault span can be a zero-width "missing token" or the whole rest of the file;
// either way the mark that helps is the one line where the parser lost the thread.
function faultPiece(state: EditorState, span: Span): Span {
  const at = Math.max(0, Math.min(span.start, state.doc.length));
  const line = state.doc.lineAt(at);
  return { start: at, end: Math.min(Math.max(span.end, at + 1), line.to) };
}

function decorationsFor(state: EditorState, spans: Spans): DecorationSet {
  const pieces = [
    ...spans.marks.flatMap((mark) =>
      perLine(state, mark.span, { class: 'ro', attributes: { title: mark.reason } })
    ),
    ...spans.hits.flatMap((hit) => perLine(state, hit, { class: 'hit' })),
    ...spans.faults.flatMap((fault) => perLine(state, faultPiece(state, fault), { class: 'bad' }))
  ].sort((left, right) => left.from - right.from || left.to - right.to);
  return Decoration.set(
    pieces.map((piece) => piece.value.range(piece.from, piece.to)),
    true
  );
}

const decorations = StateField.define<{ spans: Spans; set: DecorationSet }>({
  create: (state) => ({
    spans: { hits: [], marks: [], faults: [] },
    set: decorationsFor(state, { hits: [], marks: [], faults: [] })
  }),
  update(current, transaction) {
    const wanted = transaction.effects.find((effect) => effect.is(setSpans))?.value;
    if (!wanted && !transaction.docChanged) return current;
    const spans = wanted ?? current.spans;
    return { spans, set: decorationsFor(transaction.state, spans) };
  },
  provide: (field) => EditorView.decorations.from(field, (current) => current.set)
});

export function createEditor(
  parent: HTMLElement,
  source: string,
  writable: boolean,
  hooks: EditorHooks
): EditorView {
  const view = new EditorView({
    parent,
    state: EditorState.create({
      doc: source,
      extensions: [
        lineNumbers(),
        highlightActiveLine(),
        highlightActiveLineGutter(),
        drawSelection(),
        dropCursor(),
        history(),
        keymap.of([...defaultKeymap, ...historyKeymap, indentWithTab]),
        cpp(),
        syntaxHighlighting(highlight),
        decorations,
        theme,
        editable.of(writableExtension(writable)),
        EditorView.updateListener.of((update) => {
          const ours = !update.transactions.some((transaction) => transaction.annotation(foreign));
          if (update.docChanged && ours) hooks.onedit(update.state.doc.toString());
        })
      ]
    })
  });
  // The press, not the selection: a read-only editor has no caret to follow. It
  // has to be mousedown — the active-line highlight re-renders the line under the
  // pointer, so the browser never synthesises a click on a stable node.
  view.dom.addEventListener('mousedown', (event) => {
    // false = nearest position rather than an exact glyph hit, so a press past the
    // end of a line still lands on that line.
    hooks.onpick(view.posAtCoords({ x: event.clientX, y: event.clientY }, false));
  });
  return view;
}

export function setEditable(view: EditorView, writable: boolean): void {
  view.dispatch({ effects: editable.reconfigure(writableExtension(writable)) });
}

export function showSpans(view: EditorView, spans: Spans): void {
  view.dispatch({ effects: setSpans.of(spans), annotations: foreign.of(true) });
}

export function replaceText(view: EditorView, source: string): void {
  if (view.state.doc.toString() === source) return;
  view.dispatch({
    changes: { from: 0, to: view.state.doc.length, insert: source },
    annotations: foreign.of(true)
  });
}

export function lineOf(view: EditorView, offset: number): number {
  return view.state.doc.lineAt(Math.max(0, Math.min(offset, view.state.doc.length))).number;
}

export function jumpTo(view: EditorView, offset: number): void {
  const at = Math.max(0, Math.min(offset, view.state.doc.length));
  view.dispatch({
    selection: { anchor: at },
    effects: EditorView.scrollIntoView(at, { y: 'center' }),
    annotations: foreign.of(true)
  });
  view.focus();
}

export function revealOffset(view: EditorView, offset: number): void {
  const at = Math.max(0, Math.min(offset, view.state.doc.length));
  view.dispatch({
    effects: EditorView.scrollIntoView(at, { y: 'nearest', yMargin: 40 }),
    annotations: foreign.of(true)
  });
}
