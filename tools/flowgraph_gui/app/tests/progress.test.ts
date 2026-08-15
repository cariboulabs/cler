import { describe, expect, it } from 'vitest';
import { label, progress, remaining, type Timings } from '../src/lib/progress';

describe('phase → label', () => {
  it('names the download with real bytes', () => {
    expect(label({ phase: 'toolchain', bytes: 12_400_000, total: 24_992_393 })).toBe(
      'Downloading the compiler… 12.4 / 25.0 MB'
    );
  });

  it('names the file being compiled', () => {
    expect(label({ phase: 'compile', detail: 'desktop_examples/hello_world.cpp' })).toContain(
      'desktop_examples/hello_world.cpp'
    );
  });

  it('has a phrase for every phase', () => {
    for (const phase of ['boot', 'stage', 'link', 'optimize', 'store', 'launch'] as const) {
      expect(label({ phase }).length).toBeGreaterThan(8);
    }
  });
});

describe('phase → progress', () => {
  const cold: Timings = {};
  const warm: Timings = {
    toolchain: 20_000,
    boot: 4000,
    stage: 4000,
    compile: 12_000,
    link: 8000,
    optimize: 2000,
    store: 200,
    launch: 200
  };

  it('is determinate during the download even with no history', () => {
    const half = progress({ phase: 'toolchain', bytes: 12_496_196, total: 24_992_393 }, 0, cold);
    expect(half).toBeGreaterThan(0.1);
    expect(half).toBeLessThan(0.2);
  });

  it('shimmers (null) when there is neither bytes nor history', () => {
    expect(progress({ phase: 'compile' }, 3000, cold)).toBeNull();
  });

  it('advances monotonically through the phases once timings exist', () => {
    const seen = [
      progress({ phase: 'toolchain', bytes: 0, total: 24_992_393 }, 0, warm),
      progress({ phase: 'stage' }, 1000, warm),
      progress({ phase: 'compile' }, 6000, warm),
      progress({ phase: 'link' }, 4000, warm),
      progress({ phase: 'launch' }, 100, warm)
    ] as number[];
    for (let i = 1; i < seen.length; i++) expect(seen[i]).toBeGreaterThan(seen[i - 1]!);
    expect(seen[seen.length - 1]).toBeLessThanOrEqual(1);
  });

  it('promises no time until a previous run measured one', () => {
    expect(remaining({ phase: 'compile' }, 1000, cold)).toBeNull();
    expect(remaining({ phase: 'link' }, 4000, warm)).toBe(Math.round((4000 + 2000 + 200 + 200) / 1000));
  });
});
