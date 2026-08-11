# AI Bringup Guide for Cler DSP Framework

Context for AI assistants working in this codebase. The framework is in early
development — block names and APIs move, so treat the structure notes as a map,
not a contract, and check the code.

## 1. Overview & Architecture

Cler is a C++17 template-based DSP flowgraph framework for SDRs and embedded
systems: compile-time type safety, zero-cost abstractions, and a runtime small
enough for bare-metal MCUs.

**Platforms**: Linux and macOS fully supported. Windows only via WSL2 (no
special toolchain — WSL2 is used exactly like Linux).

**Design principles**
- Template-based: compile-time type safety and optimization
- Two execution models: flowgraph (threaded) and streamlined (manual)
- Platform agnostic: desktop, FreeRTOS, ThreadX, Zephyr, baremetal
- **Blocks own their input channels**; output channels are passed as parameters
  to `procedure()`, so a block writes into the *downstream* block's channel
- Variadic outputs via template parameter packs

**Flowgraph mode** — the framework runs blocks on threads via a task policy,
with flow control and error handling. **Streamlined mode** — you call
`procedure()` yourself in a loop; no threading, no task policy.

## 2. Repository Map

```
include/                       header-only core, link cler::cler
  cler.hpp                     Error/Result, ChannelBase/Channel, BlockBase,
                               BlockRunner, FlowGraph
  cler_spsc-queue.hpp          lock-free SPSC queue (from drogalis/SPSC-Queue)
  cler_result.hpp              Result monad (no exceptions)
  cler_utils.hpp               helpers incl. flowgraph_config::* shorthands
  cler_desktop_utils.hpp       desktop-only: cler::panic, execution report
  cler_embeddable_string.hpp   EmbeddableString<N>, no std::string
  cler_embedded_allocators.hpp
  task_policies/               desktop (std::thread), freertos, threadx, zephyr
  schedulers/                  ThreadPerBlock, FixedThreadPool, PinnedIslands
desktop_blocks/                link cler::desktop_blocks
  sources/ sinks/ math/ plots/ channelizers/ resamplers/ noise/ utils/
  gui/ ezgmsk_demod/ udp/
desktop_examples/              hello_world, flowgraph, streamlined,
                               polyphase_channelizer, SDR apps, UDP, GUI plots
embedded_examples/             baremetal, FreeRTOS, ThreadX, Zephyr
tests/                         spsc-queue/, desktop_blocks/, scheduler/ (gtest)
performance/                   cler_throughput.cpp and perf_* benchmarks
tools/cler_tools/              cler-validate, cler-viz (Python)
docs/                          web documentation
```

## 3. Build & CMake

```bash
mkdir build && cd build
cmake ..                       # Release / -O3 is the default
make -j"$(nproc --ignore=1)"
cmake -DCMAKE_BUILD_TYPE=Debug ..   # debug symbols
```

### Resource safety (mandatory)

- Before starting a build or test suite, check for existing compiler/test jobs
  and current CPU and memory pressure.
- Choose parallelism conservatively for the available machine and leave enough
  headroom for the system to remain responsive. Avoid overlapping
  resource-intensive builds or test suites.
- Read-only work, code editing, and lightweight checks may proceed concurrently.

Examples land in `build/desktop_examples/` (`hello_world`, `flowgraph`,
`polyphase_channelizer`, `streamlined`, `udp`, ...).

Two link targets:

```cmake
target_link_libraries(app PRIVATE cler::cler)            # header-only core
target_link_libraries(app PRIVATE cler::desktop_blocks)  # + GUI/plots/hardware

add_library(my_blocks INTERFACE)
target_include_directories(my_blocks INTERFACE include/)
target_link_libraries(my_blocks INTERFACE cler::cler)
```

## 4. Writing a Block

A block inherits `cler::BlockBase`, owns its input channels as members, and
implements `procedure()` taking output channels as parameters. Fixed output
count takes a concrete `cler::ChannelBase<T>*`; variable output count takes a
parameter pack.

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

### The channel count is a template parameter, never a constructor argument

`procedure()` takes its outputs as a pack, so the count is already fixed at
compile time by every call site. A runtime copy of it can only ever disagree by
mistake, and the mismatch is a buffer overrun.

```cpp
template <size_t NUM_CHANNELS, size_t FILTER_SEMILENGTH>
struct PolyphaseChannelizerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        static_assert(sizeof...(OChannels) == NUM_CHANNELS);

        auto [read_ptr, read_size] = in.read_dbf();
        if (read_size < NUM_CHANNELS) return cler::Error::NotEnoughSamples;

        std::array<std::complex<float>*, NUM_CHANNELS> ports;
        size_t min_write_space = std::numeric_limits<size_t>::max();
        size_t idx = 0;
        auto get_write_ptrs = [&](auto*... chs) {
            ([&] {
                auto [write_ptr, write_space] = chs->write_dbf();
                ports[idx] = write_ptr;
                min_write_space = std::min(min_write_space, write_space);
                idx++;
            }(), ...);
        };
        get_write_ptrs(outs...);

        const size_t num_frames = std::min(read_size / NUM_CHANNELS, min_write_space);
        if (num_frames == 0) return cler::Error::NotEnoughSpace;

        _analyzer.execute(read_ptr, num_frames, ports.data());   // whole batch, one call

        auto commit_writes = [&](auto*... chs) { ((chs->commit_write(num_frames)), ...); };
        commit_writes(outs...);
        in.commit_read(num_frames * NUM_CHANNELS);
        return cler::Empty{};
    }
};
```

**Batch the whole span, do not loop per frame.** This block got 3x faster on a
Cortex-A9 when the per-frame `liquid` call became one batched kernel — see
section 8.

### Progress Contract (mandatory)

**A successful return means the block moved at least one sample.** Schedulers
treat `cler::Empty{}` as evidence of progress: it resets the idle backoff ladder
and wakes parked workers. A `procedure()` that returns success after doing
nothing pins a core at 100% and defeats PinnedIslands entirely.

If you consumed nothing and produced nothing, return an error:
- No input → `cler::Error::NotEnoughSamples`
- No output space → `cler::Error::NotEnoughSpace`
- Either/both, don't care which → `cler::Error::NotEnoughSpaceOrSamples`

These are non-fatal; the framework retries and backs off. This applies to every
early-out: a device timeout, an overflow with no samples recovered, a
config-in-progress skip, a callback-driven block whose `procedure()` is a no-op.

**The second half of the contract: a retryable error must mean nothing was
consumed.** Those errors tell the framework "call me again"; if the block
already committed reads before bailing, that data is counted twice in the
statistics and, on multi-channel hardware, the channels silently lose alignment.
Validate every channel *before* committing on any of them.

```cpp
// WRONG - channel 0 consumed, then a retryable error reported
for (size_t i = 0; i < n; ++i) {
    if (in[i].size() < need) return cler::Error::NotEnoughSamples;
    send(in[i]); in[i].commit_read(need);
}

// RIGHT - check all, then commit all
for (size_t i = 0; i < n; ++i) {
    if (in[i].size() < need) return cler::Error::NotEnoughSamples;
}
for (size_t i = 0; i < n; ++i) { send(in[i]); in[i].commit_read(need); }
```

A block that has already made progress must report success and surface the
shortfall on the next call. Watch for paths that *compute* their way to zero — a
ratio or frame size that truncates to `0` items — not just paths guarded on an
empty channel.

```cpp
// WRONG - scheduler sees progress, never parks
size_t n = in.size();
in.commit_read(n);
return cler::Empty{};

// RIGHT
size_t n = in.size();
if (n == 0) return cler::Error::NotEnoughSamples;
in.commit_read(n);
return cler::Empty{};
```

### Errors

The full list is `enum class Error` in `cler.hpp`. Everything at or after
`TERMINATE_FLOWGRAPH` is fatal and stops the graph (`cler::is_fatal`); the
`NotEnough*` values are retryable. Use `TERM_ProcedureError` for unrecoverable
runtime failures and `cler::panic()` for unrecoverable *init* failures.

### Blocks that can block

A block whose `procedure()` can block (hardware refill, blocking I/O) declares
`static constexpr bool may_block = true;` and automatically gets a dedicated
thread instead of sharing a pool/island worker.

## 5. Channels & Buffer Access

```cpp
cler::Channel<float, 1024> static_channel;   // stack, compile-time size
cler::Channel<float>       dynamic_channel(1024);  // heap, runtime size
```

Ranked by measured performance:

1. **`read_dbf`/`write_dbf`** — true zero-copy, **the default**. Needs a
   heap channel of at least `DOUBLY_MAPPED_MIN_SIZE` (4 KB). Mandatory for
   hardware interfaces (SDRs, ADCs, DACs), best for pure data movement and
   multi-IO blocks. When doubly-mapped memory is unavailable these assert in
   debug and return `{nullptr, 0}` in release; desktop_blocks validate the size
   at construction and `cler::panic` rather than fall back silently.
2. **`readN`/`writeN`** — good baseline. Use when an external API (liquid-dsp, a
   decoder) needs its own contiguous buffer anyway.
3. **`peek_read`/`peek_write` + commit** — only ~5% faster than `readN`/`writeN`
   and easy to misuse (forgotten commit, ignored second segment). Not worth it.
   Both segments must be handled: `peek_*` returns two pointers and two sizes by
   reference, and `total = size1 + size2`.
4. **`push`/`pop`** — orders of magnitude slower. Never in a hot path.

```cpp
// readN/writeN
size_t transferable = std::min({in.size(), out->space(), BUFFER_SIZE});
in.readN(buffer, transferable);
for (size_t i = 0; i < transferable; ++i) buffer[i] *= gain;
out->writeN(buffer, transferable);

// read_dbf/write_dbf (preferred)
auto [read_ptr, read_size]   = in.read_dbf();
auto [write_ptr, write_size] = out->write_dbf();
size_t to_process = std::min(read_size, write_size);
if (to_process > 0 && read_ptr && write_ptr) {
    for (size_t i = 0; i < to_process; ++i) write_ptr[i] = read_ptr[i] * gain;
    in.commit_read(to_process);
    out->commit_write(to_process);
}
```

Each channel also carries a cumulative read counter
(`consumer_thread_cumulative_read_count()`, atomic) and write counter
(`producer_thread_cumulative_write_count()`, **not** atomic — only safe on the
producer thread). Every drain path bumps the read counter, so polling it from a
monitor thread measures throughput at zero cost, with no extra block in the
chain. Note that `space()` on a fresh channel is the *real* capacity: cler
rounds a requested size up, so occupancy percentages computed against the
requested size are wrong.

## 6. Flowgraph & Schedulers

```cpp
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

auto flowgraph = cler::make_desktop_flowgraph(
    cler::BlockRunner(&source,  &adder.in[0]),        // single output
    cler::BlockRunner(&source2, &adder.in[1]),
    cler::BlockRunner(&adder,   &throttle.in),
    cler::BlockRunner(&channelizer,                   // multiple outputs
        &plot1.in[0], &plot1.in[1], &plot1.in[2]),
    cler::BlockRunner(&plot)                          // sink, no outputs
);

cler::FlowGraphConfig config;
config.scheduler = cler::SchedulerType::ThreadPerBlock;   // default
// config.scheduler = cler::SchedulerType::FixedThreadPool;  // needs num_workers
// config.scheduler = cler::SchedulerType::PinnedIslands;    // core-constrained targets
config.num_workers = 4;
config.pin_workers = false;    // FixedThreadPool only; PinnedIslands ignores it

flowgraph.run(config);
// ... application loop ...
flowgraph.stop();
cler::print_flowgraph_execution_report(flowgraph);
```

Streamlined mode needs no task policy — call the procedures in order:

```cpp
while (true) {
    source.procedure(&adder.in0, &adder.in1);
    adder.procedure(&gain.in);
    gain.procedure(&sink.in);
    sink.procedure();
}
```

Worker-count policy (same in debug and release): `FixedThreadPool` clamps
`num_workers` up to 2, `PinnedIslands` up to 1, then both down to
`min(DEFAULT_MAX_WORKERS, regular block count)`. Zero and oversized values are
clamped, never rejected.

### ThreadPerBlock (default)
One thread per block. Simple and predictable; thread overhead grows with block
count. Best for small graphs and debugging.

### FixedThreadPool
Fixed workers process blocks round-robin. Lower thread overhead and better cache
behaviour than ThreadPerBlock, but suffers when work is imbalanced. Requires
`config.num_workers` (minimum 2). Pinning is optional via `config.pin_workers`.

### PinnedIslands
Blocks are split into contiguous topo-order islands with one pinned worker each.
Costs are measured during `calibration_ms`, then the partition is recomputed
once, then a drift check runs every `repartition_check_ms` (0 disables). Best on
core-constrained targets and imbalanced chains.

```cpp
auto cfg = cler::flowgraph_config::pinned_islands(2);   // cler_utils.hpp
cfg.calibration_ms = 500;
cfg.repartition_check_ms = 5000;
cfg.cpu_id_offset = 0;
```

- **Pinning is always attempted.** PinnedIslands pins every worker to
  `cpu_id_offset + worker_id` and does not consult `config.pin_workers`.
  Affinity failures are counted (`affinity_failure_count()`), never fatal. Real
  pinning exists only where `TaskPolicy::pin_to_core` is implemented: desktop
  and embedded **Linux** (`pthread_setaffinity_np`, so Pluto and RPi work). The
  FreeRTOS/ThreadX/Zephyr policies inherit the base implementation, which pins
  nothing and returns `false` — there the scheduler is "PinnedIslands" in name
  only. (The base used to return `true`, making a no-op indistinguishable from
  success.)
- **Telemetry**: per-block cost sampling (`block_costs()`) is collected only
  under this scheduler; the others report zeros.
- **Idle**: workers escalate through a backoff ladder, then park on a futex with
  a 1 ms timeout. `config.park_after_zero_passes` (default 4) trades wake
  latency for idle CPU.
- **Cost units**: block weight is `ns / items_moved`, derived automatically with
  no block-side annotation. Because a producer's output channel is physically
  owned by the consuming block, edge derivation already identifies each block's
  inputs; the scheduler counts input consumption where inputs resolve and falls
  back to output writes otherwise. Sources → output writes. Sinks → input reads
  (these previously collapsed to `ns/call`, a different unit). Fanout and
  channelizer → input reads, so weight does not shrink as output count grows.
  Multi-input blocks take the **max of the per-call deltas**, never the sum
  (an N-input block consumes N items for one item's worth of work) and never the
  delta of the lifetime maxima (one input holding the largest lifetime count
  while another advances would report zero). A block whose input edge failed to
  resolve, or that has a resolved input it never reads (a control channel),
  falls back to output writes rather than reporting zero.

**Observability accessors** — `partition()`, `stats()`, `block_costs()`,
`repartition_count()`, `total_park_events()`, `affinity_failure_count()` — are
exact only after `stop()` has joined the workers. During a run they are
best-effort: the cost and stats values are updated without synchronization to
the reader, and a drift repartition can rewrite `partition()` while you read it.
All are reset at the start of every `run()`.

### Repartition barrier: the invariant

`sched::RepartitionBarrier` enforces one rule: **block ownership must not change
until every regular worker has stopped executing its old island.** Each
`Channel` is an SPSC queue with exactly one reader and one writer; if a worker
still runs a block while another takes ownership, that queue briefly has two
consumers and the stream silently duplicates or reorders.

A generation counter is packed with an arrival count in one 64-bit word. Every
worker CASes its arrival. Non-leaders park on `_partition_epoch` until the
generation advances. The leader spins until `arrived == worker_count`, then
repartitions, publishes a new generation, and bumps the epoch to release the
others. `arrive()` takes `is_leader` explicitly rather than assuming worker 0,
and takes stop/wake/repartition as callables, so the barrier owns the protocol
and nothing else.

**Testing changes to it.** A bug here does not crash; it surfaces as reordered
or duplicated samples. `tests/scheduler/test_repartition_stress.cpp` drives many
repartitions under shifting per-block cost and asserts the stream stays strictly
sequential end to end. Before changing barrier code, confirm the test still
*fails* when the barrier is broken (delete the leader's wait loop — it must
report a backwards jump), and confirm it fails **repeatably**: run the broken
build 5-6 times, not once. The detector is probabilistic — an earlier, gentler
version of this test caught a broken barrier only 1 run in 6, indistinguishable
from a pass on any single run. Detection needs the heavy/light cost contrast to
be large enough that partitions genuinely change; the current constants detect
6/6.

Run it under ThreadSanitizer too. ASLR breaks TSan on recent kernels:

```bash
g++ -std=c++17 -O1 -g -fsanitize=thread -Iinclude stress.cpp -o stress -lpthread
setarch $(uname -m) -R env TSAN_OPTIONS="halt_on_error=0" ./stress
```

TSan slows execution ~10x, which suppresses the drift check — confirm the run
actually reached a high `repartition_count()` (hundreds), otherwise it never
exercised the barrier regardless of a clean report.

### Choosing a scheduler

| workload | scheduler | workers |
|---|---|---|
| simple linear chain | ThreadPerBlock | — |
| fanout, uniform paths | FixedThreadPool | min(N/2, cores) |
| fanout, imbalanced paths | PinnedIslands | CPU cores |
| >20 blocks | PinnedIslands or FixedThreadPool | 4-8 |
| sparse/intermittent data | PinnedIslands (workers park) | — |

## 7. Platform Support & Task Policies

```cpp
#include "task_policies/cler_desktop_tpolicy.hpp"   // std::thread
auto fg = cler::make_desktop_flowgraph(/* runners */);

#include "task_policies/cler_freertos_tpolicy.hpp"  // xTaskCreate
auto fg = cler::FlowGraph<cler::FreeRTOSTaskPolicy, /* runners */>(/* runners */);

#include "task_policies/cler_threadx_tpolicy.hpp"   // tx_thread_create
#include "task_policies/cler_zephyr_tpolicy.hpp"    // k_thread_create
// baremetal: no policy, streamlined mode only
```

Embedded constraints: C++17 standard library only, static allocation via
compile-time buffer sizes, no exceptions (`cler::Result` instead), template
parameters for memory control. `embedded_examples/` has one directory per
platform.

## 8. Desktop Blocks Library

**Philosophy**: generality and ease of use over minimal resource usage.
Everything that can go on the heap goes on the heap. Not tuned for tiny work
sizes.

```cpp
// sources (no input channels)
SourceCWBlock<float> cw("CW", amplitude, freq_hz, sample_rate);
SourceFileBlock<std::complex<float>> file("File", "input.bin");
SourceUDPBlock<float> udp("UDP", port, buffer_size);
SourceHackRFBlock hackrf("HackRF", center_freq, sample_rate);
SourceCaribouliteBlock caribou("Caribou", center_freq, sample_rate);

// processing
AddBlock<float, NUM_INPUTS> adder("Adder");     // input count is a template arg
GainBlock<float> gain("Gain", gain_value);
ComplexDemuxBlock demux("Demux");
PolyphaseChannelizerBlock<NUM_CHANNELS, FILTER_SEMILEN> pfb("PFB", attenuation);
MultiStageResamplerBlock<std::complex<float>> res("Res", ratio, attenuation);
RationalResamplerBlock<INTERP, DECIM, TAPS_PER_PHASE> rat("Rat", attenuation);
NoiseAWGNBlock<std::complex<float>> noise("AWGN", noise_power);
ThrottleBlock<float> throttle("Throttle", sample_rate);
FanoutBlock<float> fanout("Fanout", num_outputs);
ThroughputBlock<float> tp("Throughput");

// sinks (no output channels)
SinkFileBlock<float> file_sink("File", "output.bin");
SinkUDPBlock<float> udp_sink("UDP", host, port);
SinkNullBlock<float> null_sink("Null");
PlotTimeSeriesBlock plot("TimeSeries", {"S1", "S2"}, sample_rate, duration);
PlotCSpectrumBlock spectrum("Spectrum", {"Ch1", "Ch2"}, sample_rate, fft_size);
PlotCSpectrogramBlock spectrogram("Spectrogram", sample_rate, fft_size);
```

**Superblocks**: a desktop block may own other blocks and chain their
`procedure()` calls internally.

### Two resamplers, different jobs

`MultiStageResamplerBlock` wraps liquid's `msresamp` and takes an arbitrary
runtime ratio. `RationalResamplerBlock<INTERP, DECIM, TAPS_PER_PHASE>` is a
compile-time rational bank: zero heap, all `std::array`, one subfilter of
`TAPS_PER_PHASE` real-by-complex MACs per output over a window read in place
from the caller's span, with a `TAPS_PER_PHASE-1` carry between calls. Prefer
the rational one whenever the ratio is a fixed fraction — see section 9 for the
measured difference. Note `msresamp_crcf_get_num_output()` is a stub in the
vendored liquid (`resamp.proto.c:298` logs "not implemented" and returns 0), so
the exact output count of the liquid path cannot be queried in advance.

## 9. Measured Performance Notes

All figures below were measured, mostly on a PlutoSDR (2x Cortex-A9 @ 667 MHz).
Do not delete one without re-measuring it.

### Sizing a block's input channel against a blocking driver

A block downstream of a hardware source must give its input channel **at least
the driver's buffer size**, or the driver's refill stalls the whole graph.

`SourcePlutoBlock` allocates a 16384-sample iio buffer and `iio_buffer_refill`
blocks for roughly one buffer duration — 5.5 ms at 3 MS/s. With the polyphase
channelizer's default input channel of `DOUBLY_MAPPED_MIN_SIZE/8 * M` = 2560
samples (0.85 ms of stream at that rate) the consumer drained the channel and
starved through every refill, capping the graph at 98% of the required rate.

The symptom is diagnostic: **throughput short of the rate while the worker sits
well under 1.0 core.** That is never a compute problem. Raising the input
channel to 16385 or above fixed it with no other change.

### Confirm the graph meets the rate before comparing any CPU number

CPU-per-wall-second is not comparable between configurations that deliver
different sample counts. Every measurement must first show `msps ≈ rate`.

The cheap way to get that number needs no extra block: poll
`Channel::consumer_thread_cumulative_read_count()` from a monitor thread and
divide by wall time over a window that starts after warmup. Inserting a
pass-through counting block instead costs a thread and a full-rate memcpy, which
perturbs exactly the measurement being taken.

A worked example — the `echo_ground_station` receiver on the Pluto, asked for
3.0 MS/s, `pinned_islands(2)`, 30 s window:

| probe | measured | required |
|---|---|---|
| source alone (`pluto_smoke`) | 3.000 MS/s | 3.0 MS/s |
| `channelizer.in`, full graph | 1.513 MS/s | 3.0 MS/s |
| one `lora_rx.in`, full graph | 252.2 kS/s | 500 kS/s |

The graph ran at a self-consistent 50.4% of rate, with `channelizer.in` pinned
at 100.0% occupancy in every window, at 1.660 CPU cores of 2. The per-stage
ratio held at exactly 1/6.00 throughout, which is the check that the counters
themselves are sound. The source bracket is what separates "the radio is short"
from "the graph is short" — take it every time. Replacing the four liquid
resamplers with `RationalResamplerBlock<5,6,14>` moved the same graph to
2.862 MS/s and doubled the decoded frame rate.

**Bracket the measurement harness too.** Those runs were driven by a script that
exported `CLWB_RECEIVER_BLOCK_STATS=1`, and `collect_detailed_stats` costs
**8.1% of throughput** — the same binary with no env var does 2.862 MS/s where
the profiled run does 2.65. Enable detailed stats to compare blocks against each
other, never to establish an absolute rate.

### Scheduler: take the extra worker on a 2-core target

`SourcePluto(may_block) -> Mix -> FIR -> Sink`, 2.083 MSPS, 3 reps, spread
±0.004 cores:

| config | CPU cores | meets rate (light chain) | meets rate (loaded chain) |
|---|---|---|---|
| PinnedIslands(1) | 1.405 | yes | **no** (1.723 of 2.083 MSPS) |
| PinnedIslands(2) | 1.419 | yes | **yes** (2.078) |
| ThreadPerBlock | 1.524 | yes | no (1.897) |
| FixedThreadPool(2) | 1.544 | yes | no (1.760) |

`cores - 1` saves ~1% CPU when the chain has slack and costs 17% of capacity
when it does not. `embedded_optimized()` is `pinned_islands(2)` and is the right
default on a 2-core target.

The `may_block` source spends nearly all its time blocked in the driver
(measured at 0.196 cores for a 3 MS/s libiio source), which once read as "it
does not need a core reserved for it". A later measurement on a heavier graph
says otherwise: across all nine cuts of a 10-block chain, throughput rose with
*imbalance* and peaked where the second worker was nearly idle, 0.84 cores
against 0.16, while the best-balanced split lost 18%. A worker with slack is
what services the driver promptly. See `RECEIVER_RATE.md`.

Which partition you get is not something to leave to the cost model on a
throughput-critical graph: name it with `pinned_islands.islands` and find it by
sweeping.

The cost-based repartition is what makes 2 workers win: with the barrier
suppressed the same config drops to 1.760 MSPS. It only matters when per-block
costs are uneven — on a balanced chain it is pure overhead.

### Polyphase channelizer: batch the span, do not loop per frame

M=5, 3.0 MS/s in: the block went from 194.3 to 600.1 kS/s per port, 3.09x, by
replacing a per-frame `firpfbch_crcf_analyzer_execute` with one batched kernel.
The kernel alone is ~10x (1.33 → 13.3 MS/s on-device).

Almost none of that was arithmetic — the total real-multiply count fell only
1.7x. The rest was liquid's per-call plumbing: per 5 input samples it pushed 5
`windowcf` buffers, ran 5 dot products through a runtime-selected function
pointer, and dispatched a DFT, all to perform 30 multiply-accumulates.

The structural trick is to fold the subfilter bank over the frame index. liquid
keeps M sliding windows advancing one sample per frame; substituting
`k = M-1-i` into its own output reversal gives

```
X[k](j) = sum over n of h[n*M + M-1-k] * x[(j-n)*M + k]
```

so the taps are contiguous slices of the prototype and the data is `p`
consecutive input frames read **in place from the caller's read span**. No
windows, no per-channel state, no delay line beyond `p-1` carried frames. Both
`M` and the filter semilength are template parameters, so every buffer is a
`std::array` and the block never touches the heap.

General lesson for any liquid-backed block: if you are calling a liquid
`_execute` once per sample or once per frame, the wrapper is probably costing
more than the DSP.

### Rational resampler beats liquid's msresamp at a fixed ratio

5/6, 14 taps/phase, 80 dB, single threaded, flat across batch sizes
509 / 4096 / 16384:

| | Cortex-A9 | x86 |
|---|---|---|
| liquid `msresamp_crcf` | 1.65 MS/s | — |
| `RationalResampler` | 6.27 MS/s (3.81x) | 7.5x |

Output count is exact (50000 from 60000). Frequency response, dB relative to DC:

| tone kHz | 100 | 200 | 240 | 250 | 260 | 300 |
|---|---|---|---|---|---|---|
| ours | -0.0 | -0.9 | -4.4 | -6.0 | -8.0 | -17.3 |
| liquid | -0.0 | -0.6 | -3.4 | -4.8 | -6.5 | -14.6 |

Ours is marginally *sharper* everywhere past 200 kHz. Two traps when comparing
them: `msresamp`'s 256 phases do **not** make it a sharper filter — transition
width is set by the span in input samples, 14 taps per phase in both cases, and
the phase count only buys fractional-delay resolution that a rational ratio does
not need. And the two cannot be pinned to each other sample-for-sample:
`msresamp` is a different algorithm (fractional-delay bank with interpolation),
so correctness has to be response shape, exact output count, continuity across
batch boundaries, and end-to-end decode rate.

### ARM NEON: hand-written FP loops need -ffast-math to vectorize

The Pluto toolchain passes `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard` but
**not** `-ffast-math`. Without it GCC will not vectorize a floating-point
reduction, because NEON single-precision is not fully IEEE and reassociation is
barred. Every hand-written accumulate loop in this repo — the channelizer fold,
the rational resampler subfilter — is therefore scalar on the Pluto today.

Estimated headroom on A9 dot products is 2-4x, so this is worth measuring before
writing intrinsics by hand. Validate decode on-device after enabling it;
`-ffast-math` also changes NaN/Inf behaviour.

liquid's own dot products are *not* the problem here. `dotprod_crcf.c`
`#include`s `dotprod_crcf.neon.c`, `BUILD_NEON` is 1 in the armhf
`liquid.config.h`, and `dotprod_crcf_execute_neon{,_1,_4}` are all present in the
archive. A previous claim that liquid's CMake compiled only the portable scalar
versions was wrong.

### Benchmarks

```bash
cd build/performance
./perf_read_write_techniques     # dbf vs readN vs peek vs push/pop
./perf_simple_linear_flow        # scheduler configurations
./perf_fanout_workloads          # fanout strategies
```

## 10. Development Tools

```bash
cd tools && uv pip install -e .
cler-validate desktop_examples/*.cpp     # missing runners, bad connections
cler-viz file.cpp -o output.svg
```

### Flowgraph GUI product constraint

The GUI may place existing blocks discovered from the block library, but it
must not offer a wizard or menu action for defining a new block type. New block
types are authored directly in C++ and then discovered by the palette.

The flowgraph GUI uses Svelte 5. Keep components on the runes API (`$state`,
`$derived`, `$effect`, `$props`) and do not introduce Svelte 4 reactive syntax.

## 11. Code Style (mandatory)

- **No throw/try/catch in our code.** Recoverable runtime errors →
  `cler::Result`. Unrecoverable init/invariant failures → `cler::panic(msg)`
  (`cler_desktop_utils.hpp`; prints and aborts; desktop-only — embedded targets
  lack printf). try/catch is fine only at the boundary with external libraries
  whose intended API is exception-based (UHD, SoapySDR): catch theirs, never
  raise your own.
- **Minimal comments.** Prefer self-evident code with named constants and helper
  functions. Keep a comment only for a non-obvious constraint: a hardware quirk,
  a unit, a protocol/timing requirement, DSP math rationale.
- **Never allocate in `procedure()`.** It is the hot path. Allocate in the
  constructor, into members. Prefer `std::array` and compile-time shapes;
  C-arrays with new/delete are acceptable, and `std::vector` only where many
  failure points make RAII a real simplification.
- **Prefer `read_dbf`/`write_dbf`** over `readN`/`writeN` in `procedure()` when
  the channel is heap-allocated and ≥4 KB (always true for desktop_blocks
  defaults). Mandatory for hardware interfaces. `readN`/`writeN` is fine when an
  external API needs a separate contiguous buffer anyway.
- **Never `push`/`pop` in hot paths.**
- Templates over virtual functions on performance-critical paths; avoid
  `std::function`; composition over inheritance except for simple interfaces
  like `BlockBase`; heavy single-type implementations belong in a `.cpp`.

## 12. Common Pitfalls

1. Allocating in `procedure()`.
2. Returning `cler::Empty{}` without moving a sample — see the progress
   contract.
3. Committing a read and then returning a retryable error.
4. Missing `BlockRunner` for a block, or a type mismatch between connected
   channels.
5. Forgetting the task policy include in flowgraph mode.
6. Misusing `peek_write` — arguments are by reference, and the second segment
   must be handled.
7. Sizing a block's input channel below the upstream driver's buffer.
8. Comparing CPU numbers between runs that did not both meet the rate.
