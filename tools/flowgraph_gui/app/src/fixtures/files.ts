import { fixtures, fixtureSources } from './index';

// Repo-relative paths, matching each fixture's `file` and the palette's include roots.
const headers = import.meta.glob('../../../../../desktop_blocks/**/*.hpp', {
  query: '?raw',
  import: 'default',
  eager: true
}) as Record<string, string>;

export const browserFiles: Record<string, string> = {};
for (const [name, source] of Object.entries(fixtureSources)) {
  const file = fixtures[name]?.file;
  if (file) browserFiles[file] = source;
}
for (const [path, text] of Object.entries(headers)) {
  browserFiles[path.replace(/^(\.\.\/)+/, '')] = text;
}

// Bundled examples with a prebuilt browser build (tools/flowgraph_gui/web-run/build.sh → docs/try/run/).
export const RUNNABLE = ['hello_world', 'mass_spring_damper', 'plots', 'polyphase_channelizer'];
export const runnableExamples = RUNNABLE.flatMap((name) => {
  const path = fixtures[name]?.file;
  const source = fixtureSources[name];
  return path && source !== undefined ? [{ name, path, source }] : [];
});
