import wasmUrl from '../wasm/cler_web.wasm?url';
import { compile, link, toolchainBlocked, type Line } from './emception';
import { phase } from './progress';

type Exports = {
  memory: WebAssembly.Memory;
  cler_alloc(len: number): number;
  cler_free(ptr: number, len: number): void;
  cler_invoke(ptr: number, len: number): number;
};

const encoder = new TextEncoder();
const decoder = new TextDecoder();

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

export async function installWasmShell(
  files: Record<string, string>,
  runnable: RunnableExample[] = []
): Promise<void> {
  const invoke = await loadWasm();
  for (const [path, text] of Object.entries(files)) invoke('put_file', { path, text });
  const listeners = new Map<number, { event: string; handler: number }>();
  const callbacks = new Map<number, (payload: unknown) => void>();
  let next = 1;
  const emitEvent = (event: string, payload: unknown) => {
    for (const [id, entry] of listeners) {
      if (entry.event === event) callbacks.get(entry.handler)?.({ event, id, payload });
    }
  };
  const windows = new Map<string, { win: Window; jobId: number; timer: number }>();
  const building = new Map<string, number>();
  const currentSource = (path: string) => (invoke('open_document', { path }) as { source: string }).source;

  const job = (kind: 'check' | 'build', path: string, work: (emit: Line) => Promise<number>) => {
    const jobId = next++;
    const inputKey = { inputs: {}, recipeSha256: '' };
    if (kind === 'build') building.set(path, jobId);
    const emit: Line = (text) => {
      for (const line of String(text).split('\n')) {
        if (line.trim()) emitEvent(`${kind}-output`, { jobId, inputKey, path, line });
      }
    };
    const settle = (code: number) => {
      if (kind === 'build') building.delete(path);
      emitEvent(`${kind}-finished`, { jobId, inputKey, path, code });
    };
    void work(emit).then(settle, (error: unknown) => {
      emit(error instanceof Error ? error.message : String(error));
      settle(1);
    });
    return { jobId, inputKey };
  };
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
      const pristine = runnable.find((entry) => entry.path === path && entry.source === currentSource(path));
      const blockedReason = pristine ? null : toolchainBlocked();
      if (cmd === 'find_target') {
        if (blockedReason) throw blockedReason;
        if (pristine) {
          return {
            available: true, reason: null, name: pristine.name, buildDir: null,
            binary: `run/${pristine.name}.html`,
            artifact: { state: 'ready', artifactPath: `run/${pristine.name}.html` }
          };
        }
        const jobId = building.get(path);
        const sha = await sha256(currentSource(path));
        const artifactPath = `built/${sha}/app.html`;
        return {
          available: true, reason: null,
          name: path.split('/').pop()?.replace(/\.[^.]+$/, '') ?? 'flowgraph',
          buildDir: null, binary: null,
          artifact: jobId !== undefined
            ? { state: 'building', jobId }
            : (await cached(sha))
              ? { state: 'ready', artifactPath }
              : { state: 'needs_build', reason: 'compile this document in the browser first (Ctrl+B)' }
        };
      }
      if (cmd === 'check_document') {
        if (toolchainBlocked()) throw toolchainBlocked();
        return job('check', path, (emit) => compile(files, path, currentSource(path), emit));
      }
      if (cmd === 'build_target') {
        if (blockedReason) throw blockedReason;
        return job('build', path, async (emit) => {
          const source = currentSource(path);
          const sha = await sha256(source);
          if (await cached(sha)) {
            emit(`built/${sha}/app.html is already built — press Run`);
            return 0;
          }
          const code = await compile(files, path, source, emit);
          if (code !== 0) return code;
          const linked = await link(emit);
          if (linked.code !== 0) return linked.code;
          phase({ phase: 'store' });
          await store(sha, linked.files);
          return 0;
        });
      }
      if (cmd === 'run_target') {
        let target = `run/${pristine?.name}.html`;
        if (!pristine) {
          const sha = await sha256(currentSource(path));
          if (!(await cached(sha))) throw 'this edit is not built yet — press Build (Ctrl+B) first';
          target = `built/${sha}/app.html`;
        }
        const jobId = next++;
        phase({ phase: 'launch' });
        const opened = window.open(target, '_blank', 'popup,width=1280,height=800');
        if (!opened) throw 'the browser blocked the run window — allow popups for this site';
        const inputKey = { inputs: {}, recipeSha256: '' };
        const timer = window.setInterval(() => {
          if (!opened.closed) return;
          window.clearInterval(timer);
          windows.delete(path);
          emitEvent('run-finished', { jobId, inputKey, path, code: 0 });
        }, 500);
        windows.set(path, { win: opened, jobId, timer });
        emitEvent('run-output', { jobId, inputKey, path, line: `running ${target} in a new window — close it or press Stop` });
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

const BUILD_CACHE = 'cler-built';
const KEEP_BUILDS = 5;
const buildCache = () => caches.open(BUILD_CACHE);
const originPath = (path: string) => new URL(path, location.href).pathname;

async function cached(sha: string): Promise<boolean> {
  return !!(await buildCache().then((cache) => cache.match(originPath(`built/${sha}/app.html`))));
}

async function store(sha: string, artifacts: Record<string, Uint8Array>): Promise<void> {
  const cache = await buildCache();
  for (const [name, data] of Object.entries(artifacts)) {
    await cache.put(originPath(`built/${sha}/${name}`), new Response(data as BlobPart));
  }
  // ponytail: ~4 MB a build, so keep the last few in insertion order; an LRU would need access times Cache Storage does not keep.
  const keys = await cache.keys();
  const shas = [...new Set(keys.map((request) => new URL(request.url).pathname.split('/built/')[1]?.split('/')[0]))];
  for (const stale of shas.slice(0, Math.max(0, shas.length - KEEP_BUILDS))) {
    for (const request of keys) if (request.url.includes(`/built/${stale}/`)) await cache.delete(request);
  }
}

async function sha256(text: string): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, '0')).join('').slice(0, 16);
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
