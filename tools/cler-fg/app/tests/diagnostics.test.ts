import { describe, expect, it } from 'vitest';
import { fixtures, fixtureSources } from '../src/fixtures';
import {
  compileProblems,
  offsetOfLine,
  parseDiagnostics,
  placeDiagnostics,
  type Diagnostic
} from '../src/lib/diagnostics';
import { lineOfOffset, type Site } from '../src/lib/schema';

const PATH = '/tmp/fake/hello_world.cpp';

function sites(name: string): Site[] {
  const model = structuredClone(fixtures[name]);
  if (!model) throw new Error(`no fixture ${name}`);
  return model.sites;
}

function sourceOf(name: string): string {
  const source = fixtureSources[name];
  if (source === undefined) throw new Error(`no source ${name}`);
  return source;
}

function lineOf(name: string, blockVar: string): number {
  const site = sites(name)[0];
  const block = site?.blocks.find((candidate) => candidate.var === blockVar);
  if (!block) throw new Error(`no block ${blockVar}`);
  return lineOfOffset(sourceOf(name), block.span.start);
}

function runnerLine(name: string, blockVar: string): number {
  const site = sites(name)[0];
  const runner = site?.runners.find((candidate) => candidate.block === blockVar);
  if (!runner) throw new Error(`no runner for ${blockVar}`);
  return lineOfOffset(sourceOf(name), runner.span.start);
}

function place(lines: string[], name = 'hello_world') {
  return placeDiagnostics(parseDiagnostics(lines), PATH, sourceOf(name), sites(name));
}

/* ============================================================ the gcc parser */

describe('the gcc diagnostic parser', () => {
  it('reads the file, line, column, severity and message', () => {
    const found = parseDiagnostics([
      `${PATH}: In function ‘int main()’:`,
      `${PATH}:13:46: error: unable to find numeric literal operator ‘operator""ff’`,
      '   13 |     SourceCWBlock<float> source1("CWSource", 1.0ff, 1.0f, SPS);',
      '      |                                              ^~~~~'
    ]);
    expect(found).toHaveLength(1);
    expect(found[0]).toEqual<Diagnostic>({
      file: PATH,
      line: 13,
      column: 46,
      severity: 'error',
      message: 'unable to find numeric literal operator ‘operator""ff’',
      notes: []
    });
  });

  it('attaches following notes to the diagnostic above them', () => {
    const found = parseDiagnostics([
      `${PATH}:13:46: error: no matching function for call to ‘ThrottleBlock()’`,
      `/usr/include/c++/11/throttle.hpp:31:5: note: candidate expects 2 arguments, 1 provided`,
      `/usr/include/c++/11/throttle.hpp:44:5: note: candidate is not viable`
    ]);
    expect(found).toHaveLength(1);
    expect(found[0]?.notes).toEqual([
      '/usr/include/c++/11/throttle.hpp:31: candidate expects 2 arguments, 1 provided',
      '/usr/include/c++/11/throttle.hpp:44: candidate is not viable'
    ]);
  });

  it('separates warnings from errors and keeps fatal errors as errors', () => {
    const found = parseDiagnostics([
      `${PATH}:9:5: warning: unused variable ‘gui’ [-Wunused-variable]`,
      `${PATH}:3:10: fatal error: desktop_blocks/sources/source_cw.hpp: No such file or directory`,
      'compilation terminated.'
    ]);
    expect(found.map((entry) => entry.severity)).toEqual(['warning', 'error']);
    expect(found[1]?.message).toContain('No such file or directory');
  });

  it('ignores lines that are not diagnostics', () => {
    expect(parseDiagnostics(['', 'make: *** [all] Error 1', 'In file included from x.cpp:2:'])).toEqual(
      []
    );
  });
});

/* ============================================================ the line mapper */

describe('mapping a diagnostic line onto the model', () => {
  it('finds the block whose declaration owns the line', () => {
    const line = lineOf('hello_world', 'source1');
    const [placed] = place([`${PATH}:${line}:46: error: bad literal`]);
    expect(placed?.block).toBe('source1');
    expect(placed?.site).toBe(0);
    expect(placed?.span).not.toBeNull();
  });

  it('finds the block whose runner owns the line', () => {
    const line = runnerLine('hello_world', 'throttle');
    const [placed] = place([`${PATH}:${line}:9: error: no matching call`]);
    expect(placed?.block).toBe('throttle');
  });

  it('leaves a diagnostic from another file unplaced', () => {
    const [placed] = place(['/usr/include/c++/11/vector:120:7: error: template argument 1 is invalid']);
    expect(placed?.block).toBeNull();
    expect(placed?.site).toBeNull();
    expect(placed?.span).toBeNull();
  });

  it('keeps a line outside every site file-level but still jumpable', () => {
    const [placed] = place([`${PATH}:1:1: error: expected declaration`]);
    expect(placed?.block).toBeNull();
    expect(placed?.span).toEqual({ start: 0, end: sourceOf('hello_world').indexOf('\n') });
  });

  it('falls back to the nearest element for a line inside the site but between runners', () => {
    const source = sourceOf('hello_world');
    const site = sites('hello_world')[0];
    const runner = site?.runners[1];
    if (!runner) throw new Error('no second runner');
    const indent = source.lastIndexOf('\n', runner.span.start) + 1;
    const [placed] = place([`${PATH}:${lineOfOffset(source, indent)}:1: error: no matching call`]);
    expect(placed?.block).toBe('source1');
    expect(placed?.site).toBe(0);
  });

  it('turns placed diagnostics into problems and drops the foreign ones', () => {
    const line = lineOf('hello_world', 'source1');
    const problems = compileProblems(
      place([
        `${PATH}:${line}:46: error: bad literal`,
        '/usr/include/c++/11/vector:120:7: error: elsewhere'
      ])
    );
    expect(problems).toHaveLength(1);
    expect(problems[0]?.kind).toBe('compile');
    expect(problems[0]?.severity).toBe('error');
    expect(problems[0]?.title).toBe('source1');
    expect(problems[0]?.detail).toBe('bad literal');
    expect(problems[0]?.block).toBe('source1');
  });
});

/* ============================================================ offsets */

describe('line and column offsets', () => {
  it('walks to the requested line and column', () => {
    const source = 'one\ntwo\nthree\n';
    expect(offsetOfLine(source, 1, 1)).toBe(0);
    expect(offsetOfLine(source, 2, 1)).toBe(4);
    expect(offsetOfLine(source, 2, 3)).toBe(6);
    expect(offsetOfLine(source, 99, 1)).toBe(source.length);
  });
});
