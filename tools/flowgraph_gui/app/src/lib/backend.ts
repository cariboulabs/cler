import { invoke } from '@tauri-apps/api/core';
import { listen, type UnlistenFn } from '@tauri-apps/api/event';
import { open } from '@tauri-apps/plugin-dialog';
import { fixtures } from '../fixtures';
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

export function onExternalChange(handler: (path: string) => void): Promise<UnlistenFn> {
  return listen<{ path: string }>(EXTERNAL_CHANGE_EVENT, (event) => handler(event.payload.path));
}

export function loadFixture(name: string): DocumentState {
  const model = fixtures[name];
  if (!model) throw new Error(`unknown fixture: ${name}`);
  return {
    path: model.file,
    revision: 0,
    model,
    canUndo: false,
    canRedo: false,
    externalChange: false
  };
}

function asRecord(value: unknown): Record<string, unknown> | null {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) return null;
  return value as Record<string, unknown>;
}

function asText(value: unknown): string | null {
  return typeof value === 'string' && value.length > 0 ? value : null;
}

function phrase(value: unknown): string {
  return asText(value)?.replace(/_/g, ' ') ?? 'unknown error';
}

function rawText(rejection: unknown): string {
  if (typeof rejection === 'string') return rejection;
  if (rejection instanceof Error) return rejection.message;
  return String(rejection);
}

function parsed(rejection: unknown): Record<string, unknown> | null {
  const direct = asRecord(rejection);
  if (direct) return direct;
  try {
    return asRecord(JSON.parse(rawText(rejection)));
  } catch {
    return null;
  }
}

function plural(count: number, noun: string): string {
  return `${count} ${noun}${count === 1 ? '' : 's'}`;
}

export function describeApplyError(rejection: unknown): string {
  const record = parsed(rejection);
  const kind = record ? asText(record.error) : null;
  if (!record || !kind) return rawText(rejection);

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
      return 'the file changed since this edit started — reload it and try again';
    case 'index_out_of_range':
      return `${phrase(record.element)} index ${String(record.index)} is out of range (${String(record.len)})`;
    case 'no_display_name_argument':
      return `${asText(record.block) ?? 'this block'} has no display-name argument to edit`;
    case 'file_has_errors':
      return 'the file has parse errors — fix them before editing';
    default: {
      const detail =
        asText(record.element) ??
        asText(record.block) ??
        asText(record.text) ??
        asText(record.var_name);
      return detail ? `${phrase(kind)}: ${detail}` : phrase(kind);
    }
  }
}
