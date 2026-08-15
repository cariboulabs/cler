// The C++ toolchain, in the browser: emception (clang + lld + the emscripten sysroot,
// compiled to wasm) runs em++ over a virtual repo, so /try can compile and link an edited
// flowgraph with no server. Self-hosting is a base-URL swap: mirror the files emception's
// Pages site serves next to this app and point TOOLCHAIN_BASE at them (public/cler-sw.js
// reads it off its own registration URL and mirrors it under emception/*, which is what
// makes the cross-origin worker bundle loadable at all).
import * as Comlink from 'comlink';

export const TOOLCHAIN_BASE = 'https://jprendes.github.io/emception/';

// The bundle below runs as same-origin code, so it is pinned: cler-sw.js verifies these four
// sha256s before caching, and refuses the file otherwise. Recompute after a TOOLCHAIN_BASE bump
// with `curl -sL <base><name> | sha256sum`.
export const TOOLCHAIN_PINS: Record<string, string> = {
  'emception.worker.bundle.worker.js': '60b9f0fb7982f9395ef63872b5ed3b798377fab09a8666f28b67ccb5029c0107',
  'f0283badd42fe745cbe4.wasm': '2c60c515eca756e80ddc752a6ac062e07f596eb70c7a1308321705f90e09b442',
  '9d1e542b80004e27297f.wasm': '47a2b00defa938d4471ff6ffdbf4d424ee03599db7d8f56590c6223e96191631',
  'cecdfcda360457a8f204.br': '9bd873132b4915a4da34a977a386a4ae68785df34b8cdb9c3d205fae26eeb772'
};

// Virtual repo root, and emception's cwd — so every path on the em++ command line is the
// app's own repo-relative path, and so are the paths in the diagnostics coming back.
const ROOT = '/working';
const OBJ = 'draft.o';
const ARTIFACTS = ['app.html', 'app.js', 'app.wasm', 'app.worker.js'];

// Must stay in step with tools/flowgraph_gui/web-run/build.sh — the archives were built with these.
const CXXFLAGS = [
  '-std=c++17', '-O2', '-pthread', '-Iinclude', '-I.', '-Idesktop_blocks/gui', '-Idesktop_blocks/plots',
  '-Ibuild/_deps/imgui-src', '-Ibuild/_deps/imgui-src/backends', '-Ibuild/_deps/implot-src', '-Iliquid',
  '-DIMGUI_IMPL_OPENGL_ES3', '-DImDrawIdx=unsigned int',
  '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-missing-braces', '-Wno-deprecated-declarations'
];
// ponytail: -O1 links in ~10 s where -O2 takes ~2 min; raise it if the run window turns out too slow.
// -sMINIFY_HTML=0 is not optional: emception ships no html-minifier.
const LDFLAGS = [
  '-O1', '-pthread', '-sUSE_GLFW=3', '-sUSE_WEBGL2=1', '-sFULL_ES3=1', '-sALLOW_MEMORY_GROWTH=1',
  '-sPTHREAD_POOL_SIZE=8', '-sASYNCIFY', '-sASYNCIFY_STACK_SIZE=65536', '-sEXIT_RUNTIME=0',
  '-sMINIFY_HTML=0', '--shell-file', 'shell.html'
];

type Emception = {
  init(): Promise<void>;
  run(...argv: string[]): Promise<{ returncode: number }>;
  fileSystem: {
    mkdirTree(path: string): Promise<void>;
    writeFile(path: string, data: Uint8Array): Promise<void>;
    readFile(path: string): Promise<Uint8Array>;
  };
  onstdout: unknown;
  onstderr: unknown;
};

export type Line = (text: string) => void;

const encoder = new TextEncoder();
// Absolute: dynamic import() and new Worker() resolve against this module's URL (assets/), not the page.
const base = () => new URL(import.meta.env.BASE_URL, location.href).href;
const INIT_TIMEOUT_MS = 60_000;
let booting: Promise<Emception> | null = null;
let sink: Line = () => {};
let blocked: string | null = null;

// Set by main.ts when the service worker cannot be registered (private windows, disabled
// workers): without it there is no cross-origin isolation and no toolchain mirror, so the
// editor still runs but Check/Build/Run have to say why they cannot.
export function blockToolchain(reason: string): void {
  blocked = reason;
}

export function toolchainBlocked(): string | null {
  return blocked;
}

export function compile(
  files: Record<string, string>,
  path: string,
  source: string,
  onLine: Line
): Promise<number> {
  return boot(files, onLine).then(async (em) => {
    await write(em, path, encoder.encode(source));
    return exec(em, ['em++', ...CXXFLAGS, '-c', path, '-o', OBJ], onLine);
  });
}

export async function link(onLine: Line): Promise<{ code: number; files: Record<string, Uint8Array> }> {
  if (!booting) throw new Error('the C++ toolchain is not running — compile first');
  const em = await booting;
  const argv = ['em++', OBJ, 'lib/libcler_web.a', 'lib/libliquid.a', ...LDFLAGS, '-o', 'app.html'];
  const code = await exec(em, argv, onLine);
  const out: Record<string, Uint8Array> = {};
  if (code === 0) {
    for (const name of ARTIFACTS) out[name] = new Uint8Array(await em.fileSystem.readFile(`${ROOT}/${name}`));
  }
  return { code, files: out };
}

function boot(files: Record<string, string>, onLine: Line): Promise<Emception> {
  if (blocked) return Promise.reject(new Error(blocked));
  sink = onLine;
  // Reset on failure so the next Check/Build retries instead of awaiting a promise that will
  // never settle (offline, a moved upstream hash, a proxy in the way).
  booting ??= start(files).catch((error: unknown) => {
    booting = null;
    throw error;
  });
  return booting;
}

async function start(files: Record<string, string>): Promise<Emception> {
  let bytes = 0;
  let reported: string | null = null;
  const progress = (event: MessageEvent) => {
    const data = event.data as { toolchain?: string; bytes?: number; toolchainError?: string };
    if (data?.toolchainError) {
      reported = data.toolchainError;
      sink(data.toolchainError);
    } else if (data?.toolchain) {
      bytes += data.bytes ?? 0;
      sink(`downloading the C++ toolchain (first visit only)… ${(bytes / 1e6).toFixed(1)} MB`);
    }
  };
  navigator.serviceWorker.addEventListener('message', progress);
  sink('starting the in-browser C++ toolchain…');
  const worker = new Worker(`${base()}emception/emception.worker.bundle.worker.js`);
  const em = Comlink.wrap(worker) as unknown as Emception;
  em.onstdout = Comlink.proxy((text: string) => sink(text));
  em.onstderr = Comlink.proxy((text: string) => sink(text));
  let timer = 0;
  // em.init() simply never settles when a toolchain file does not arrive, so race it.
  const stalled = new Promise<never>((_, reject) => {
    worker.onerror = (event) =>
      reject(new Error(reported ?? `the C++ toolchain worker failed: ${event.message || 'load error'}`));
    timer = self.setTimeout(
      () => reject(new Error(reported ?? `the C++ toolchain did not start within ${INIT_TIMEOUT_MS / 1000} s`)),
      INIT_TIMEOUT_MS
    );
  });
  try {
    await Promise.race([em.init(), stalled]);
    sink('unpacking the cler headers and libraries…');
    await upload(em, files);
  } catch (error) {
    worker.terminate();
    throw error;
  } finally {
    clearTimeout(timer);
    navigator.serviceWorker.removeEventListener('message', progress);
  }
  return em;
}

async function upload(em: Emception, files: Record<string, string>): Promise<void> {
  const headers = (await (await fetch(`${base()}payload/headers.json`)).json()) as Record<string, string>;
  for (const [path, text] of Object.entries({ ...headers, ...files })) {
    await write(em, path, encoder.encode(text));
  }
  for (const lib of ['libcler_web.a', 'libliquid.a']) {
    const data = await (await fetch(`${base()}payload/${lib}`)).arrayBuffer();
    await write(em, `lib/${lib}`, new Uint8Array(data));
  }
}

async function write(em: Emception, path: string, data: Uint8Array): Promise<void> {
  await em.fileSystem.mkdirTree(`${ROOT}/${path}`.replace(/\/[^/]+$/, ''));
  await em.fileSystem.writeFile(`${ROOT}/${path}`, data);
}

async function exec(em: Emception, argv: string[], onLine: Line): Promise<number> {
  sink = onLine;
  onLine(`$ ${argv[0]} … ${argv[argv.length - 2]} ${argv[argv.length - 1]}`);
  return (await em.run(...argv)).returncode;
}
