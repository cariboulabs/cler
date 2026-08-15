// Browser backend: cler-web.wasm behind the Tauri IPC surface, so the app runs the
// full editor with no server. Same JSON invoke shape as e2e_backend / webshim.
import wasmUrl from '../wasm/cler_web.wasm?url';

type Exports = {
  memory: WebAssembly.Memory;
  cler_alloc(len: number): number;
  cler_free(ptr: number, len: number): void;
  cler_invoke(ptr: number, len: number): number;
};

const encoder = new TextEncoder();
const decoder = new TextDecoder();

// Only the WASI calls Rust std actually links here; anything else would show up as
// a LinkError at instantiate time, not silently misbehave.
function wasi(getMemory: () => WebAssembly.Memory) {
  const view = () => new DataView(getMemory().buffer);
  const bytes = () => new Uint8Array(getMemory().buffer);
  return {
    random_get(ptr: number, len: number) {
      crypto.getRandomValues(bytes().subarray(ptr, ptr + len));
      return 0;
    },
    environ_get() {
      return 0;
    },
    environ_sizes_get(countPtr: number, sizePtr: number) {
      view().setUint32(countPtr, 0, true);
      view().setUint32(sizePtr, 0, true);
      return 0;
    },
    clock_time_get(_id: number, _precision: bigint, out: number) {
      view().setBigUint64(out, BigInt(Math.round(performance.now() * 1e6)), true);
      return 0;
    },
    fd_close() {
      return 0;
    },
    fd_seek() {
      return 70; // ESPIPE
    },
    fd_write(fd: number, iovs: number, count: number, written: number) {
      let total = 0;
      let text = '';
      for (let i = 0; i < count; i++) {
        const ptr = view().getUint32(iovs + i * 8, true);
        const len = view().getUint32(iovs + i * 8 + 4, true);
        text += decoder.decode(bytes().subarray(ptr, ptr + len));
        total += len;
      }
      view().setUint32(written, total, true);
      (fd === 2 ? console.error : console.log)(text);
      return 0;
    },
    proc_exit(code: number) {
      throw new Error(`cler-web.wasm exited with ${code}`);
    }
  };
}

export type Invoke = (cmd: string, args: Record<string, unknown>) => unknown;

export async function loadWasm(): Promise<Invoke> {
  return bindWasm((imports) => WebAssembly.instantiateStreaming(fetch(wasmUrl), imports));
}

export async function bindWasm(
  instantiate: (imports: WebAssembly.Imports) => Promise<WebAssembly.WebAssemblyInstantiatedSource>
): Promise<Invoke> {
  let exports: Exports | null = null;
  const { instance } = await instantiate({ wasi_snapshot_preview1: wasi(() => exports!.memory) });
  exports = instance.exports as unknown as Exports;
  const wasm = exports;
  return (cmd, args) => {
    const request = encoder.encode(JSON.stringify({ cmd, args }));
    const inPtr = wasm.cler_alloc(request.length);
    new Uint8Array(wasm.memory.buffer).set(request, inPtr);
    const outPtr = wasm.cler_invoke(inPtr, request.length);
    wasm.cler_free(inPtr, request.length);
    const heap = new Uint8Array(wasm.memory.buffer);
    let end = outPtr;
    while (heap[end] !== 0) end++;
    const reply = JSON.parse(decoder.decode(heap.subarray(outPtr, end)));
    wasm.cler_free(outPtr, end - outPtr + 1);
    if ('loud' in reply) throw new Error(reply.loud);
    if ('err' in reply) throw reply.err;
    return reply.ok;
  };
}

// Installs window.__TAURI_INTERNALS__ over the wasm so backend.ts sees a desktop shell.
export async function installWasmShell(files: Record<string, string>): Promise<void> {
  const invoke = await loadWasm();
  for (const [path, text] of Object.entries(files)) invoke('put_file', { path, text });
  const listeners = new Map<number, string>();
  let next = 1;
  const win = window as unknown as Record<string, unknown>;
  win.__TAURI_INTERNALS__ = {
    invoke: async (cmd: string, args: Record<string, unknown> = {}) => {
      if (cmd === 'plugin:dialog|open' || cmd === 'plugin:dialog|save') return null;
      if (cmd === 'plugin:event|listen') {
        const id = next++;
        listeners.set(id, args.event as string);
        return id;
      }
      if (cmd === 'plugin:event|unlisten') {
        listeners.delete(args.eventId as number);
        return null;
      }
      const result = invoke(cmd, args);
      if (cmd === 'save_document') download(args.path as string, (result as { source: string }).source);
      return result;
    },
    transformCallback: (_callback: unknown) => next++,
    metadata: {}
  };
  win.__TAURI_EVENT_PLUGIN_INTERNALS__ = { unregisterListener: () => undefined };
}

// ponytail: "save" in the browser hands the file to the visitor; there is no disk to write.
function download(path: string, source: string): void {
  const name = path.split('/').pop() ?? 'flowgraph.cpp';
  const url = URL.createObjectURL(new Blob([source], { type: 'text/plain' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  link.click();
  URL.revokeObjectURL(url);
}
