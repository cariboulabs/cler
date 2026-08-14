# AI Bringup Guide for Cler DSP Framework

Cler is a C++17 template-based DSP flowgraph framework for SDRs and embedded
systems: compile-time type safety, zero-cost abstractions, runtime small enough
for bare-metal MCUs. Early development — check the code, not just this map.
**Platforms**: Linux and macOS; Windows only via WSL2 (used exactly like Linux).

Two execution models: **flowgraph** (framework runs blocks on threads via a
task policy) and **streamlined** (you call `procedure()` yourself in a loop).
**Blocks own their input channels**; outputs are passed as parameters to
`procedure()`, so a block writes into the *downstream* block's channel.
Variadic outputs via template parameter packs.

## Repository Map

```
include/                 header-only core: cler.hpp (Error/Result, Channel,
                         BlockBase, FlowGraph), cler_utils.hpp (flowgraph_config
                         shorthands), cler_desktop_utils.hpp (cler::panic),
                         task_policies/, schedulers/
desktop_blocks/          sources/sinks/math/plots/channelizers/resamplers/...
desktop_examples/        hello_world, flowgraph, streamlined, SDR apps, GUI
                         (binaries land in build/desktop_examples/)
embedded_examples/       baremetal, FreeRTOS, ThreadX, Zephyr
tests/                   spsc-queue/, desktop_blocks/, scheduler/ (gtest)
performance/             perf_* benchmarks; tools/cler_tools/ cler-validate,
                         cler-viz (Python)
tools/flowgraph_gui/     GRC-style GUI (has its own AGENTS.md)
```

## Build

```bash
mkdir build && cd build && cmake .. && make -j"$(nproc --ignore=1)"  # Release/-O3 default
```
Choose parallelism conservatively; don't overlap heavy builds/test suites.

```cmake
target_link_libraries(app PRIVATE cler::cler)            # header-only core
target_link_libraries(app PRIVATE cler::desktop_blocks)  # + GUI/plots/hardware
```

## Writing a Block

Inherit `cler::BlockBase`, own input channels as members, implement `procedure()`
taking outputs (`cler::ChannelBase<T>*` for fixed count, pack for variable).

```cpp
struct GainBlock : public cler::BlockBase {
    cler::Channel<float> in;
    GainBlock(const char* name, float gain)
        : BlockBase(name), in(BUFFER_SIZE), _gain(gain) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        auto [rptr, rsize] = in.read_dbf();
        auto [wptr, wsize] = out->write_dbf();
        size_t n = std::min(rsize, wsize);
        if (n == 0) return cler::Error::NotEnoughSamples;
        for (size_t i = 0; i < n; ++i) wptr[i] = rptr[i] * _gain;
        in.commit_read(n);
        out->commit_write(n);
        return cler::Empty{};
    }
private:
    float _gain;
};
```

**Channel count is a template parameter, never a constructor argument.** The
pack fixes it at compile time; a runtime copy can only disagree by mistake, and
the mismatch is a buffer overrun (`static_assert` the pack size against it).

**Errors**: `enum class Error` in `cler.hpp`. At/after `TERMINATE_FLOWGRAPH`
is fatal (`cler::is_fatal`); `NotEnough*` retryable. `TERM_ProcedureError` for
unrecoverable runtime failures, `cler::panic()` for unrecoverable init.

**Blocking blocks**: if `procedure()` can block (hardware refill, blocking
I/O), declare `static constexpr bool may_block = true;` — dedicated thread.

**GUI blocks**: declare `static constexpr bool is_gui = true;` and implement
`void render()`. Anything drawing or acting every frame is an `is_gui` block,
control panels included; `GuiManager::render(flowgraph)` calls each `render()`
in runner order (typical app: `flowgraph.run();` then `while
(!gui.should_close()) gui.render(flowgraph);`). A GUI-only block has no
channels; its `procedure()` returns `NotEnoughSamples` so the scheduler parks
it. `render()` runs on the GUI thread concurrently with `procedure()` — shared
state uses the plot blocks' snapshot/atomic pattern.

### Progress Contract (mandatory)

**A successful return means the block moved at least one sample.** Schedulers
treat `cler::Empty{}` as progress (resets idle backoff, wakes parked workers);
returning success after doing nothing pins a core at 100%. If nothing moved,
return `NotEnoughSamples` / `NotEnoughSpace` / `NotEnoughSpaceOrSamples` —
non-fatal, the framework retries with backoff. Applies to every early-out:
device timeouts, config skips, no-ops, paths that *compute* their way to zero.

**A retryable error must mean nothing was consumed** — otherwise data is
double-counted and multi-channel hardware silently loses alignment. **Check
all channels, then commit all**; never commit one channel and then bail
retryably on another. A block that already made progress reports success and
surfaces the shortfall next call.

## Channels & Buffer Access

`Channel<float, 1024>` is stack/compile-time size; `Channel<float> ch(1024)`
is heap/runtime size. Access methods ranked by measured performance:
1. **`read_dbf`/`write_dbf`** — zero-copy, **the default**; mandatory for
   hardware interfaces. Needs a heap channel ≥ `DOUBLY_MAPPED_MIN_SIZE` (4 KB);
   otherwise asserts in debug, returns `{nullptr, 0}` in release.
2. **`readN`/`writeN`** — fine when an external API (liquid-dsp, a decoder)
   needs its own contiguous buffer anyway.
3. **`peek_read`/`peek_write`** — ~5% over readN, easy to misuse (by-reference
   args, second segment must be handled). Avoid.
4. **`push`/`pop`** — orders of magnitude slower. Never in a hot path.

A block downstream of a hardware source needs an input channel **at least the
driver's buffer size**, or refills starve the graph (symptom: short of rate
with the worker well under 1.0 core). `consumer_thread_cumulative_read_count()`
(atomic) measures throughput for free from a monitor thread.

## Flowgraph & Schedulers

Include `task_policies/cler_desktop_tpolicy.hpp`; wire with
`cler::make_desktop_flowgraph(cler::BlockRunner(&src, &gain.in),
cler::BlockRunner(&gain, &sink.in), ...)`, then `fg.run(config)`/`fg.stop()`.

- **ThreadPerBlock** (default): one thread per block; small graphs, debugging.
- **FixedThreadPool**: fixed round-robin workers; needs `config.num_workers`
  (min 2); suffers on imbalanced work.
- **PinnedIslands**: contiguous topo-order islands, one pinned worker each;
  cost-calibrated, repartitions on drift, workers park when idle; best on
  core-constrained targets (real pinning only on Linux task policies).

| workload | scheduler |
|---|---|
| simple linear chain | ThreadPerBlock |
| fanout, uniform paths | FixedThreadPool, min(N/2, cores) workers |
| fanout, imbalanced paths | PinnedIslands, workers = cores |
| >20 blocks | PinnedIslands or FixedThreadPool |
| sparse/intermittent data | PinnedIslands |

`cler::flowgraph_config::pinned_islands(2)` (= `embedded_optimized()`) is the
right default on a 2-core target. Repartition invariant: block ownership must
not change until every worker stopped executing its old island (channels are
SPSC — two consumers silently duplicate/reorder); see
`tests/scheduler/test_repartition_stress.cpp` before touching barrier code.
Embedded: FreeRTOS/ThreadX/Zephyr task policies; baremetal is streamlined-mode
only, static allocation via compile-time buffer sizes, no exceptions.

## Measured Lessons

- Batch liquid calls per span, not per frame — wrapper overhead dwarfs the DSP.
- `RationalResamplerBlock<I,D,TAPS>` beats liquid `msresamp` ~4-7x at a fixed
  ratio; prefer it whenever the ratio is a fixed fraction.
- Take the extra worker on 2-core: `pinned_islands(2)` meets rate where
  `pinned_islands(1)` loses 17% of capacity under load.
- Confirm the graph meets the sample rate before comparing any CPU number.
- Hand-written FP loops need `-ffast-math` to vectorize on NEON (it changes
  NaN/Inf behavior — validate decode after enabling).

## Tools

```bash
cd tools && uv pip install -e .
cler-validate desktop_examples/*.cpp   # missing runners, bad connections
cler-viz file.cpp -o output.svg
```

**Flowgraph GUI**: read `tools/flowgraph_gui/AGENTS.md` before editing there.
Product constraint: the GUI places existing blocks discovered from the library
but must not offer a new-block wizard — new block types are authored in C++.
Svelte 5 runes API only (`$state`, `$derived`, `$effect`, `$props`).

**Screenshots**: `GuiManager::request_screenshot(path)` grabs the next frame
(prefer `.png`). `spike` exposes it: `./spike --capture /tmp/shots
--capture-force --capture-exit --capture-no-dat` (needs a real `DISPLAY`).

## Code Style (mandatory)

- No throw/try/catch: `cler::Result` for recoverable errors, `cler::panic()`
  for init failures; try/catch only at exception-based external libs (UHD).
- Never allocate in `procedure()` — allocate in the constructor, into members;
  prefer `std::array` and compile-time shapes.
- Prefer `read_dbf`/`write_dbf`; never `push`/`pop` in hot paths.
- Minimal comments — only non-obvious constraints (hardware quirks, units,
  protocol/timing, DSP math).
- Templates over virtuals on hot paths; avoid `std::function`; composition
  over inheritance except simple interfaces like `BlockBase`.

## Common Pitfalls

1. Allocating in `procedure()`.
2. Returning `cler::Empty{}` without moving a sample (progress contract).
3. Committing a read, then returning a retryable error.
4. Missing `BlockRunner`, or type mismatch between connected channels.
5. Forgetting the task policy include in flowgraph mode.
6. Misusing `peek_write` (by-reference args, second segment).
7. Input channel sized below the upstream driver's buffer.
8. Comparing CPU numbers between runs that did not both meet the rate.
