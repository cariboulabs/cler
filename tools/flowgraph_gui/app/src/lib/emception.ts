// The C++ toolchain, in the browser: emception (clang + lld + the emscripten sysroot,
// compiled to wasm) runs em++ over a virtual repo, so /try can compile and link an edited
// flowgraph with no server. Self-hosting is a base-URL swap: mirror the files emception's
// Pages site serves next to this app and point TOOLCHAIN_BASE at them (public/cler-sw.js
// reads it off its own registration URL and mirrors it under emception/*, which is what
// makes the cross-origin worker bundle loadable at all).
import * as Comlink from 'comlink';

export const TOOLCHAIN_BASE = 'https://jprendes.github.io/emception/';

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
let booting: Promise<Emception> | null = null;
let sink: Line = () => {};

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
  const em = await booting!;
  const argv = ['em++', OBJ, 'lib/libcler_web.a', 'lib/libliquid.a', ...LDFLAGS, '-o', 'app.html'];
  const code = await exec(em, argv, onLine);
  const out: Record<string, Uint8Array> = {};
  if (code === 0) {
    for (const name of ARTIFACTS) out[name] = new Uint8Array(await em.fileSystem.readFile(`${ROOT}/${name}`));
  }
  return { code, files: out };
}

function boot(files: Record<string, string>, onLine: Line): Promise<Emception> {
  sink = onLine;
  booting ??= start(files);
  return booting;
}

async function start(files: Record<string, string>): Promise<Emception> {
  let bytes = 0;
  const progress = (event: MessageEvent) => {
    if (!(event.data as { toolchain?: string })?.toolchain) return;
    bytes += (event.data as { bytes: number }).bytes;
    sink(`downloading the C++ toolchain (first visit only)… ${(bytes / 1e6).toFixed(1)} MB`);
  };
  navigator.serviceWorker.addEventListener('message', progress);
  sink('starting the in-browser C++ toolchain…');
  const em = Comlink.wrap(new Worker(`${base()}emception/emception.worker.bundle.worker.js`)) as unknown as Emception;
  em.onstdout = Comlink.proxy((text: string) => sink(text));
  em.onstderr = Comlink.proxy((text: string) => sink(text));
  try {
    await em.init();
    sink('unpacking the cler headers and libraries…');
    await upload(em, files);
  } finally {
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
