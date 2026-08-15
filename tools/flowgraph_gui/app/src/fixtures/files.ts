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
