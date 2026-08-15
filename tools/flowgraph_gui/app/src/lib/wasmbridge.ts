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

export type RunnableExample = { name: string; path: string; source: string };

// Installs window.__TAURI_INTERNALS__ over the wasm so backend.ts sees a desktop shell.
// `runnable` lists bundled examples with a prebuilt browser build under run/<name>.html;
// Run pops that build in a new window (the desktop pops a GLFW window) while the source
// still matches the bundle. Compiling edits in the browser is the next step.
export async function installWasmShell(
  files: Record<string, string>,
  runnable: RunnableExample[] = []
): Promise<void> {
  const invoke = await loadWasm();
  for (const [path, text] of Object.entries(files)) invoke('put_file', { path, text });
  const listeners = new Map<number, { event: string; handler: number }>();
  const callbacks = new Map<number, (payload: unknown) => void>();
  let next = 1;
  const emit = (event: string, payload: unknown) => {
    for (const [id, entry] of listeners) {
      if (entry.event === event) callbacks.get(entry.handler)?.({ event, id, payload });
    }
  };
  const windows = new Map<string, { win: Window; jobId: number; timer: number }>();
  const currentSource = (path: string) => (invoke('open_document', { path }) as { source: string }).source;
  const win = window as unknown as Record<string, unknown>;
  win.__TAURI_INTERNALS__ = {
    invoke: async (cmd: string, args: Record<string, unknown> = {}) => {
      if (cmd === 'plugin:dialog|open' || cmd === 'plugin:dialog|save') return null;
      if (cmd === 'plugin:event|listen') {
        const id = next++;
        listeners.set(id, { event: args.event as string, handler: args.handler as number });
        return id;
      }
      if (cmd === 'plugin:event|unlisten') {
        listeners.delete(args.eventId as number);
        return null;
      }
      const path = args.path as string;
      const example = runnable.find((entry) => entry.path === path);
      if (cmd === 'find_target') {
        if (!example) throw `no browser build for ${path} — Run works on the bundled examples`;
        if (currentSource(path) !== example.source) {
          throw 'compiling your edits in the browser lands next — Run works on the unmodified example (undo or reload to get back)';
        }
        return {
          available: true,
          reason: null,
          name: example.name,
          buildDir: null,
          binary: `run/${example.name}.html`,
          artifact: { state: 'ready', artifactPath: `run/${example.name}.html` }
        };
      }
      if (cmd === 'run_target') {
        if (!example) throw `no browser build for ${path}`;
        const jobId = next++;
        const opened = window.open(`run/${example.name}.html`, '_blank', 'popup,width=1280,height=800');
        if (!opened) throw 'the browser blocked the run window — allow popups for this site';
        const inputKey = { inputs: {}, recipeSha256: '' };
        const timer = window.setInterval(() => {
          if (!opened.closed) return;
          window.clearInterval(timer);
          windows.delete(path);
          emit('run-finished', { jobId, inputKey, path, code: 0 });
        }, 500);
        windows.set(path, { win: opened, jobId, timer });
        emit('run-output', { jobId, inputKey, path, line: `running run/${example.name}.html in a new window — close it or press Stop` });
        return { jobId, inputKey };
      }
      if (cmd === 'stop_target') {
        windows.get(path)?.win.close();
        return null;
      }
      const result = invoke(cmd, args);
      if (cmd === 'save_document') download(path, (result as { source: string }).source);
      return result;
    },
    transformCallback: (callback: (payload: unknown) => void) => {
      const id = next++;
      callbacks.set(id, callback);
      return id;
    },
    metadata: {}
  };
  win.__TAURI_EVENT_PLUGIN_INTERNALS__ = {
    unregisterListener: (_event: string, eventId: number) => listeners.delete(eventId)
  };
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
