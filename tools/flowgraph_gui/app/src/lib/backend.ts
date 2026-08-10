import { invoke } from '@tauri-apps/api/core';
import { open } from '@tauri-apps/plugin-dialog';
import { fixtures } from '../fixtures';
import type { ParseResult } from './schema';

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

export async function parseFile(path: string): Promise<ParseResult> {
  const json = await invoke<string>('parse_file', { path });
  return JSON.parse(json) as ParseResult;
}

export function loadFixture(name: string): ParseResult {
  const fixture = fixtures[name];
  if (!fixture) throw new Error(`unknown fixture: ${name}`);
  return fixture;
}
