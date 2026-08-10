import { invoke } from '@tauri-apps/api/core';
import { listen, type UnlistenFn } from '@tauri-apps/api/event';
import { open } from '@tauri-apps/plugin-dialog';
import { fixtures, fixtureSources } from '../fixtures';
import type { BlockSpec } from './palette';
import type { Command, DocumentState } from './schema';

export type Invoker = (command: string, args: Record<string, unknown>) => Promise<unknown>;

const EXTERNAL_CHANGE_EVENT = 'document-changed-externally';

let invoker: Invoker = (command, args) => invoke(command, args);

export function setInvoker(next: Invoker): void {
  invoker = next;
}

export function inTauri(): boolean {
  return typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;
}

export async function pickFile(): Promise<string | null> {
  const picked = await open({
    multiple: false,
    directory: false,
    filters: [{ name: 'C++ source', extensions: ['cpp', 'cc', 'cxx'] }]
  });
  return typeof picked === 'string' ? picked : null;
}

const tails = new Map<string, Promise<unknown>>();

export function queued<T>(path: string, task: () => Promise<T>): Promise<T> {
  const tail = tails.get(path) ?? Promise.resolve();
  const next = tail.then(task, task);
  tails.set(
    path,
    next.then(
      () => undefined,
      () => undefined
    )
  );
  return next;
}

async function documentCall(command: string, args: Record<string, unknown>): Promise<DocumentState> {
  return (await invoker(command, args)) as DocumentState;
}

export function openDocument(path: string): Promise<DocumentState> {
  return documentCall('open_document', { path });
}

export async function closeDocument(path: string): Promise<void> {
  await invoker('close_document', { path });
}

export function reloadDocument(path: string): Promise<DocumentState> {
  return documentCall('reload_document', { path });
}

export function undoDocument(path: string): Promise<DocumentState> {
  return documentCall('undo', { path });
}

export function redoDocument(path: string): Promise<DocumentState> {
  return documentCall('redo', { path });
}

export function applyCommands(
  path: string,
  commands: Command[],
  baseRevision: number
): Promise<DocumentState> {
  return documentCall('apply_commands', { path, commands, baseRevision });
}

export async function loadPalette(path: string): Promise<BlockSpec[]> {
  const specs = await invoker('palette', { path });
  return Array.isArray(specs) ? (specs as BlockSpec[]) : [];
}

export function onExternalChange(handler: (path: string) => void): Promise<UnlistenFn> {
  return listen<{ path: string }>(EXTERNAL_CHANGE_EVENT, (event) => handler(event.payload.path));
}

export function loadFixture(name: string): DocumentState {
  const model = fixtures[name];
  const source = fixtureSources[name];
  if (!model || source === undefined) throw new Error(`unknown fixture: ${name}`);
  return {
    path: model.file,
    revision: 0,
    model,
    source,
    canUndo: false,
    canRedo: false,
    externalChange: false
  };
}

export function openInEditor(path: string, line: number): Promise<void> {
  return invoker('open_in_editor', { path, line }) as Promise<void>;
}

function asRecord(value: unknown): Record<string, unknown> | null {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) return null;
  return value as Record<string, unknown>;
}

const NO_REASON = 'the edit was refused but no reason was given';
const READABLE = /[a-z]{3}/i;
const MAX_RAW = 200;

function asText(value: unknown): string | null {
  return typeof value === 'string' && value.trim().length > 0 ? value : null;
}

function phrase(value: unknown): string {
  return asText(value)?.replace(/_/g, ' ') ?? 'unknown error';
}

function plainText(rejection: unknown): string | null {
  if (typeof rejection === 'string') return rejection;
  if (rejection instanceof Error) return rejection.message;
  return null;
}

function parsed(rejection: unknown): Record<string, unknown> | null {
  const text = plainText(rejection);
  if (text === null) return asRecord(rejection);
  try {
    return asRecord(JSON.parse(text));
  } catch {
    return null;
  }
}

function plural(count: number, noun: string): string {
  return `${count} ${noun}${count === 1 ? '' : 's'}`;
}

function clipped(text: string): string {
  return text.length <= MAX_RAW ? text : `${text.slice(0, MAX_RAW)}…`;
}

function asJson(value: unknown): string | null {
  try {
    const text = JSON.stringify(value);
    return typeof text === 'string' ? clipped(text) : null;
  } catch {
    return null;
  }
}

function detailOf(record: Record<string, unknown>): string | null {
  return (
    asText(record.detail) ??
    asText(record.message) ??
    asText(record.reason) ??
    asText(record.element) ??
    asText(record.block) ??
    asText(record.text) ??
    asText(record.var_name)
  );
}

function refused(what: string | null): string {
  return what === null ? NO_REASON : `the backend refused: ${what}`;
}

function undescribed(rejection: unknown, record: Record<string, unknown> | null): string {
  if (record) return refused(detailOf(record) ?? asJson(record));
  const text = plainText(rejection)?.trim() ?? '';
  if (text.length === 0) return NO_REASON;
  return READABLE.test(text) ? text : refused(clipped(text));
}

export function errorRecord(rejection: unknown): Record<string, unknown> | null {
  return parsed(rejection);
}

export function spansOf(record: Record<string, unknown> | null): { start: number; end: number }[] {
  if (!record || !Array.isArray(record.spans)) return [];
  return record.spans.filter(
    (span): span is { start: number; end: number } =>
      typeof span === 'object' &&
      span !== null &&
      typeof (span as { start: unknown }).start === 'number' &&
      typeof (span as { end: unknown }).end === 'number'
  );
}

export function describeApplyError(rejection: unknown): string {
  const record = parsed(rejection);
  const kind = record ? asText(record.error) : null;
  if (!record || !kind) return undescribed(rejection, record);

  switch (kind) {
    case 'not_editable': {
      const reason = asText(record.reason);
      const element = asText(record.element) ?? 'this element';
      return reason ? `${element} is read-only: ${phrase(reason)}` : `${element} is read-only`;
    }
    case 'invalid_expression':
      return `"${asText(record.text) ?? ''}" is not a valid expression`;
    case 'references_outside_graph': {
      const block = asText(record.block) ?? 'this block';
      const count = Array.isArray(record.spans) ? record.spans.length : 0;
      return `${block} is still used in ${plural(count, 'place')} outside the flowgraph — remove those references first`;
    }
    case 'revision_mismatch':
      return 'the graph changed under this gesture — try again';
    case 'index_out_of_range':
      return `${phrase(record.element)} index ${String(record.index)} is out of range (${String(record.len)})`;
    case 'no_display_name_argument':
      return `${asText(record.block) ?? 'this block'} has no display-name argument to edit`;
    case 'file_has_errors':
      return 'the file has parse errors — fix them before editing';
    default: {
      const detail = detailOf(record);
      return detail ? `${phrase(kind)}: ${detail}` : phrase(kind);
    }
  }
}
