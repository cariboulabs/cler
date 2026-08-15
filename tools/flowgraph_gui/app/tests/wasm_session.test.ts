import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { bindWasm } from '../src/lib/wasmbridge';

const WASM = join(__dirname, '../src/wasm/cler_web.wasm');
const HELLO = join(__dirname, '../../../../desktop_examples/hello_world.cpp');
const GAIN = join(__dirname, '../../../../desktop_blocks/math/gain.hpp');

describe('cler_web.wasm session', () => {
  // built by ./build-wasm.sh (needs WASI_SDK); absent in a plain checkout
  it.skipIf(!existsSync(WASM))('opens, places a block with its include, and undoes it', async () => {
    const invoke = await bindWasm((imports) => WebAssembly.instantiate(readFileSync(WASM), imports));
    invoke('put_file', { path: 'desktop_examples/hello_world.cpp', text: readFileSync(HELLO, 'utf8') });
    invoke('put_file', { path: 'desktop_blocks/math/gain.hpp', text: readFileSync(GAIN, 'utf8') });

    const opened = invoke('open_document', { path: 'desktop_examples/hello_world.cpp' }) as {
      revision: number;
      model: { sites: unknown[] };
      source: string;
    };
    expect(opened.model.sites.length).toBeGreaterThan(0);
    const palette = invoke('palette', { path: 'desktop_examples/hello_world.cpp' }) as { name: string }[];
    expect(palette.map((spec) => spec.name)).toContain('GainBlock');

    const placed = invoke('apply_commands', {
      path: 'desktop_examples/hello_world.cpp',
      baseRevision: opened.revision,
      commands: [{ command: 'add_block', site: 0, type: 'GainBlock', template_args: ['float'], ctor_args: ['"gain"', '2.0f'], var_name: 'gain' }]
    }) as { source: string; canUndo: boolean };
    expect(placed.source).toContain('GainBlock');
    expect(placed.source).toContain('#include "desktop_blocks/math/gain.hpp"');
    expect(placed.canUndo).toBe(true);

    const undone = invoke('undo', { path: 'desktop_examples/hello_world.cpp' }) as { source: string; dirty: boolean };
    expect(undone.source).toBe(opened.source);
    expect(undone.dirty).toBe(false);

    expect(() => invoke('open_in_editor', { path: 'desktop_examples/hello_world.cpp', line: 1 })).toThrow(
      /desktop app/
    );
  });
});
