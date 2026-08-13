import { invoke } from '@tauri-apps/api/core';
import { listen, type UnlistenFn } from '@tauri-apps/api/event';
import { open, save } from '@tauri-apps/plugin-dialog';
import { fixtures, fixtureSources } from '../fixtures';
import type { BlockSpec } from './palette';
import type { Command, DocumentState } from './schema';

export type Invoker = (command: string, args: Record<string, unknown>) => Promise<unknown>;

const EXTERNAL_CHANGE_EVENT = 'document-changed-externally';
const ARTIFACT_STATUS_EVENT = 'artifact-status-changed';

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

export async function pickFolder(): Promise<string | null> {
  const picked = await open({ multiple: false, directory: true });
  return typeof picked === 'string' ? picked : null;
}

export async function pickSavePath(defaultPath?: string): Promise<string | null> {
  const picked = await save({
    defaultPath,
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

export type NodeMove = {
  node: string;
  from: { x: number; y: number };
  to: { x: number; y: number };
};

export function moveNodes(path: string, view: string, moves: NodeMove[]): Promise<DocumentState> {
  return documentCall('move_nodes', { path, view, moves });
}

export function saveDocument(path: string): Promise<DocumentState> {
  return documentCall('save_document', { path });
}

export function saveDocumentAs(path: string, newPath: string): Promise<DocumentState> {
  return documentCall('save_document_as', { path, newPath });
}

export function newDocument(path: string): Promise<DocumentState> {
  return documentCall('new_document', { path });
}

export type AppSettings = {
  clerRoot: string | null;
  blockLibraries: string[];
  aiAgentModel: string | null;
  aiAgentProvider: string | null;
  aiAgentBaseUrl: string | null;
};

export function appSettings(): Promise<AppSettings> {
  return invoker('app_settings', {}) as Promise<AppSettings>;
}

export function setAppSettings(next: AppSettings): Promise<AppSettings> {
  return invoker('set_app_settings', { next }) as Promise<AppSettings>;
}

export function resolvedClerRoot(path: string): Promise<string | null> {
  return invoker('resolved_cler_root', { path }) as Promise<string | null>;
}

export function saveCache(path: string, ui: unknown): Promise<void> {
  return invoker('save_cache', { path, ui }) as Promise<void>;
}

export function applyCommands(
  path: string,
  commands: Command[],
  baseRevision: number
): Promise<DocumentState> {
  return documentCall('apply_commands', { path, commands, baseRevision });
}

export type PreviewResult = { diff: string; summary: { splices: number } };

export function previewCommands(
  path: string,
  commands: Command[],
  baseRevision: number
): Promise<PreviewResult> {
  return invoker('preview_commands', { path, commands, baseRevision }) as Promise<PreviewResult>;
}

export async function loadPalette(path: string): Promise<BlockSpec[]> {
  const specs = await invoker('palette', { path });
  return Array.isArray(specs) ? (specs as BlockSpec[]) : [];
}

export function onExternalChange(handler: (path: string) => void): Promise<UnlistenFn> {
  return listen<{ path: string }>(EXTERNAL_CHANGE_EVENT, (event) => handler(event.payload.path));
}

export function onArtifactStatusChange(handler: (path: string) => void): Promise<UnlistenFn> {
  return listen<{ path: string }>(ARTIFACT_STATUS_EVENT, (event) => handler(event.payload.path));
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
    dirty: false,
    externalChange: false,
    cache: {}
  };
}

export function openInEditor(path: string, line: number): Promise<void> {
  return invoker('open_in_editor', { path, line }) as Promise<void>;
}

export const TASKS = ['check', 'build', 'run'] as const;

export type TaskKind = (typeof TASKS)[number];

export type InputKey = { inputs: Record<string, string>; recipeSha256: string };

export type TaskStarted = { jobId: number; inputKey: InputKey };

export type TaskLine = TaskStarted & { path: string; line: string };

export type TaskEnd = TaskStarted & { path: string; code: number | null };

export type ArtifactStatus =
  | { state: 'unavailable'; reason: string }
  | { state: 'needs_build'; reason: string }
  | { state: 'building'; jobId: number }
  | { state: 'ready'; artifactPath: string };

export type TargetInfo = {
  available: boolean;
  reason: string | null;
  name: string;
  buildDir: string | null;
  binary: string | null;
  artifact: ArtifactStatus;
};

export function checkDocument(path: string): Promise<TaskStarted> {
  return invoker('check_document', { path }) as Promise<TaskStarted>;
}

export function findTarget(path: string): Promise<TargetInfo> {
  return invoker('find_target', { path }) as Promise<TargetInfo>;
}

export function buildTarget(path: string): Promise<TaskStarted> {
  return invoker('build_target', { path }) as Promise<TaskStarted>;
}

export function runTarget(path: string, args: string[] = []): Promise<TaskStarted> {
  return invoker('run_target', { path, args }) as Promise<TaskStarted>;
}

export function stopTarget(path: string): Promise<void> {
  return invoker('stop_target', { path }) as Promise<void>;
}

export function onTaskLine(
  handler: (kind: TaskKind, payload: TaskLine) => void
): Promise<UnlistenFn[]> {
  return Promise.all(
    TASKS.map((kind) =>
      listen<TaskLine>(`${kind}-output`, (event) => handler(kind, event.payload))
    )
  );
}

export function onTaskEnd(
  handler: (kind: TaskKind, payload: TaskEnd) => void
): Promise<UnlistenFn[]> {
  return Promise.all(
    TASKS.map((kind) =>
      listen<TaskEnd>(`${kind}-finished`, (event) => handler(kind, event.payload))
    )
  );
}

export type AiAgentStatus = {
  available: boolean;
  provider: string;
  model: string;
  reason: string | null;
  method: 'api_key' | 'oauth' | null;
};

export type AiAgentDelta = { path: string; text: string };

export type AiAgentDone = {
  path: string;
  usage: { input_tokens: number; output_tokens: number };
  error: string | null;
};

export function aiAgentStatus(): Promise<AiAgentStatus> {
  return invoker('ai_agent_status', {}) as Promise<AiAgentStatus>;
}

export type ListedModel = {
  id: string;
  name: string;
  context: number;
  input_cost: number;
  output_cost: number;
};

export function aiAgentModels(): Promise<ListedModel[]> {
  return invoker('ai_agent_models', {}) as Promise<ListedModel[]>;
}

export function aiAgentSetKey(key: string): Promise<AiAgentStatus> {
  return invoker('ai_agent_set_key', { key }) as Promise<AiAgentStatus>;
}

export function aiAgentOauthStart(): Promise<string> {
  return invoker('ai_agent_oauth_start', {}) as Promise<string>;
}

export function aiAgentOauthFinish(input: string): Promise<AiAgentStatus> {
  return invoker('ai_agent_oauth_finish', { input }) as Promise<AiAgentStatus>;
}

export function aiAgentOauthLogout(): Promise<AiAgentStatus> {
  return invoker('ai_agent_oauth_logout', {}) as Promise<AiAgentStatus>;
}

export function onAiAgentAuthChanged(
  handler: (status: AiAgentStatus) => void
): Promise<UnlistenFn> {
  return listen<AiAgentStatus>('ai-agent-auth-changed', (event) => handler(event.payload));
}

export function openKeyConsole(): Promise<void> {
  return invoker('open_key_console', {}) as Promise<void>;
}

export function aiAgentAsk(
  path: string,
  question: string,
  history: { role: string; text: string }[],
  selected: string | null
): Promise<void> {
  return invoker('ai_agent_ask', { path, question, history, selected }) as Promise<void>;
}

export function aiAgentStop(path: string): Promise<void> {
  return invoker('ai_agent_stop', { path }) as Promise<void>;
}

export function onAiAgentDelta(
  handler: (payload: AiAgentDelta) => void
): Promise<UnlistenFn> {
  return listen<AiAgentDelta>('ai-agent-delta', (event) => handler(event.payload));
}

export function onAiAgentDone(handler: (payload: AiAgentDone) => void): Promise<UnlistenFn> {
  return listen<AiAgentDone>('ai-agent-done', (event) => handler(event.payload));
}

export type AiAgentProposal = {
  path: string;
  rationale: string;
  commands: Command[];
  dropped: number;
};

export function onAiAgentProposal(
  handler: (payload: AiAgentProposal) => void
): Promise<UnlistenFn> {
  return listen<AiAgentProposal>('ai-agent-proposal', (event) => handler(event.payload));
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
