import * as Comlink from 'comlink';
import { phase } from './progress';

export const TOOLCHAIN_BASE = 'https://jprendes.github.io/emception/';

export const TOOLCHAIN_PINS: Record<string, string> = {
  'emception.worker.bundle.worker.js': '60b9f0fb7982f9395ef63872b5ed3b798377fab09a8666f28b67ccb5029c0107',
  'f0283badd42fe745cbe4.wasm': '2c60c515eca756e80ddc752a6ac062e07f596eb70c7a1308321705f90e09b442',
  '9d1e542b80004e27297f.wasm': '47a2b00defa938d4471ff6ffdbf4d424ee03599db7d8f56590c6223e96191631',
  'cecdfcda360457a8f204.br': '9bd873132b4915a4da34a977a386a4ae68785df34b8cdb9c3d205fae26eeb772'
};

export const TOOLCHAIN_BYTES = 24_992_393;

const VIRTUAL_REPO_ROOT = '/working';
const OBJ = 'draft.o';
const ARTIFACTS = ['app.html', 'app.js', 'app.wasm', 'app.worker.js'];

const CXXFLAGS = [
  '-std=c++17', '-O2', '-pthread', '-Iinclude', '-I.', '-Idesktop_blocks/gui', '-Idesktop_blocks/plots',
  '-Ibuild/_deps/imgui-src', '-Ibuild/_deps/imgui-src/backends', '-Ibuild/_deps/implot-src', '-Iliquid',
  '-DIMGUI_IMPL_OPENGL_ES3', '-DImDrawIdx=unsigned int',
  '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-missing-braces', '-Wno-deprecated-declarations'
];
// ponytail: -O1 links in ~10 s where -O2 takes ~2 min; raise it if the run window turns out too slow.
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
const assetBaseUrl = () => new URL(import.meta.env.BASE_URL, location.href).href;
const STALL_TIMEOUT_MS = 60_000;
let booting: Promise<Emception> | null = null;
let sink: Line = () => {};
let blocked: string | null = null;

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
    phase({ phase: 'compile', detail: path });
    return exec(em, ['em++', ...CXXFLAGS, '-c', path, '-o', OBJ], onLine);
  });
}

export async function link(onLine: Line): Promise<{ code: number; files: Record<string, Uint8Array> }> {
  if (!booting) throw new Error('the C++ toolchain is not running — compile first');
  const em = await booting;
  const argv = ['em++', OBJ, 'lib/libcler_web.a', 'lib/libliquid.a', ...LDFLAGS, '-o', 'app.html'];
  phase({ phase: 'link' });
  let wasmOptStarted = false;
  const code = await exec(em, argv, (text) => {
    if (!wasmOptStarted && /wasm-opt/.test(text)) {
      wasmOptStarted = true;
      phase({ phase: 'optimize' });
    }
    onLine(text);
  });
  const out: Record<string, Uint8Array> = {};
  if (code === 0) {
    for (const name of ARTIFACTS) out[name] = new Uint8Array(await em.fileSystem.readFile(`${VIRTUAL_REPO_ROOT}/${name}`));
  }
  return { code, files: out };
}

function boot(files: Record<string, string>, onLine: Line): Promise<Emception> {
  if (blocked) return Promise.reject(new Error(blocked));
  sink = onLine;
  booting ??= start(files).catch((error: unknown) => {
    booting = null;
    throw error;
  });
  return booting;
}

async function start(files: Record<string, string>): Promise<Emception> {
  let bytes = 0;
  let lastToolchainActivity = Date.now();
  let workerReportedError: string | null = null;
  const onToolchainMessage = (event: MessageEvent) => {
    const data = event.data as { toolchain?: string; bytes?: number; toolchainError?: string };
    if (data?.toolchainError) {
      workerReportedError = data.toolchainError;
      sink(data.toolchainError);
    } else if (data?.toolchain) {
      bytes += data.bytes ?? 0;
      lastToolchainActivity = Date.now();
      phase(bytes >= TOOLCHAIN_BYTES ? { phase: 'boot' } : { phase: 'toolchain', bytes, total: TOOLCHAIN_BYTES });
      sink(`downloading the C++ toolchain (first visit only)… ${(bytes / 1e6).toFixed(1)} MB`);
    }
  };
  navigator.serviceWorker.addEventListener('message', onToolchainMessage);
  sink('starting the in-browser C++ toolchain…');
  phase({ phase: 'boot' });
  const worker = new Worker(`${assetBaseUrl()}emception/emception.worker.bundle.worker.js`);
  const em = Comlink.wrap(worker) as unknown as Emception;
  em.onstdout = Comlink.proxy((text: string) => sink(text));
  em.onstderr = Comlink.proxy((text: string) => sink(text));
  let stallWatch = 0;
  const stalled = new Promise<never>((_, reject) => {
    worker.onerror = (event) =>
      reject(new Error(workerReportedError ?? `the C++ toolchain worker failed: ${event.message || 'load error'}`));
    stallWatch = self.setInterval(() => {
      if (Date.now() - lastToolchainActivity < STALL_TIMEOUT_MS) return;
      reject(
        new Error(
          workerReportedError ??
            `the C++ toolchain stalled for ${STALL_TIMEOUT_MS / 1000} s with no download progress`
        )
      );
    }, 1000);
  });
  try {
    await Promise.race([em.init(), stalled]);
    sink('unpacking the cler headers and libraries…');
    phase({ phase: 'stage' });
    await upload(em, files);
  } catch (error) {
    worker.terminate();
    throw error;
  } finally {
    clearInterval(stallWatch);
    navigator.serviceWorker.removeEventListener('message', onToolchainMessage);
  }
  return em;
}

async function upload(em: Emception, files: Record<string, string>): Promise<void> {
  const headers = (await (await fetch(`${assetBaseUrl()}payload/headers.json`)).json()) as Record<string, string>;
  phase({ phase: 'stage', detail: `${Object.keys(headers).length} headers` });
  for (const [path, text] of Object.entries({ ...headers, ...files })) {
    await write(em, path, encoder.encode(text));
  }
  for (const lib of ['libcler_web.a', 'libliquid.a']) {
    phase({ phase: 'stage', detail: lib });
    const data = await (await fetch(`${assetBaseUrl()}payload/${lib}`)).arrayBuffer();
    await write(em, `lib/${lib}`, new Uint8Array(data));
  }
}

async function write(em: Emception, path: string, data: Uint8Array): Promise<void> {
  await em.fileSystem.mkdirTree(`${VIRTUAL_REPO_ROOT}/${path}`.replace(/\/[^/]+$/, ''));
  await em.fileSystem.writeFile(`${VIRTUAL_REPO_ROOT}/${path}`, data);
}

async function exec(em: Emception, argv: string[], onLine: Line): Promise<number> {
  sink = onLine;
  onLine(`$ ${argv[0]} … ${argv[argv.length - 2]} ${argv[argv.length - 1]}`);
  return (await em.run(...argv)).returncode;
}
