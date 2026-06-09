# Cler Scheduler Latency Stress Plan

> **DECISION UPDATE (supersedes parts of this note):** Cler is **lossless** — it
> never drops, overwrites, or reorders samples; all measurements are critical.
> The premise below ("stale queued messages worse than dropped") and the entire
> freshness/drop direction (§C — Add Freshness Channel Policies, drop counters,
> TTL, keep-latest) are therefore **out of scope**. The answer to a slow consumer
> is backpressure (`push` busy-waits, `try_push` fails) or a faster consumer, not
> dropping. The benchmarks, idle-contract audit, idle policy, backpressure
> instrumentation, and *lossless* contention-aware scheduling remain in scope.
> See `scheduler_latency_impl_plan.md` for the current, authoritative plan.

This note is about Cler itself, not any downstream application. The goal is to
test whether Cler can support low-latency message pipelines where stale queued
messages are worse than dropped messages.

The current framework is mostly optimized for throughput and sample
preservation. That is right for continuous DSP streams, but not always right for
control messages, telemetry, socket datagrams, UI events, or protocol bridges.

## Problem Statement

For a low-rate message sink, we want to know:

- How long can an item sit in a Cler channel before the consumer block runs?
- Under CPU pressure, does the terminal sink thread get scheduled quickly enough?
- Do idle blocks waste CPU and make latency worse?
- When a downstream sink is slow, can the framework express "drop stale data"
  instead of preserving FIFO backlog?
- Are scheduler changes measurable against the current behavior?

The answer should come from benchmarks, not intuition.

## Current Behavior To Baseline

Current `FlowGraphConfig` defaults:

- `scheduler = ThreadPerBlock`
- `adaptive_sleep = false`
- `max_calls_per_tick = 4`
- `collect_detailed_stats = false`

Important current semantics:

- `Empty{}` means the scheduler treats the procedure call as progress.
- `NotEnoughSamples`, `NotEnoughSpace`, and `NotEnoughSpaceOrSamples` mean no
  progress and trigger idle handling.
- With `adaptive_sleep = false`, `handle_adaptive_sleep()` returns immediately,
  so thread-per-block workers retry without an intentional sleep.
- `DesktopTaskPolicy::relax()` spins briefly and then sleeps for 1 us, but it
  is not used by the default `ThreadPerBlock` no-work path when adaptive sleep
  is disabled. `FixedThreadPool` does call `relax()` when a worker finds no work
  across its assigned blocks.
- `Channel::push()` busy-waits until space exists.
- `Channel::try_push()` fails when full.
- There is no built-in channel age, high-watermark, stale-drop, or drop counter.

## Metrics

Every experiment should report these metrics:

- `throughput_items_per_s`
- `p50_enqueue_to_pop_us`
- `p90_enqueue_to_pop_us`
- `p99_enqueue_to_pop_us`
- `max_enqueue_to_pop_us`
- `p99_procedure_gap_us` per block, meaning time between successful consumer
  procedures
- `channel_high_watermark`
- `dropped_newest`
- `dropped_oldest`
- `stale_dropped`
- `cpu_percent_total`
- `cpu_percent_per_thread` when practical
- `context_switches` when practical

Use `std::chrono::steady_clock` for benchmark-local timing. Keep wall-clock time
out of the framework-level benchmarks.

## Baseline Benchmarks

### 1. Low-Rate Message Pipeline

Purpose: measure scheduling latency when messages are sparse.

Graph:

```text
PeriodicMessageSource -> MessageSink
```

Source behavior:

- Emits one message every `period_us`.
- Message contains `sequence`, `steady_clock enqueue timestamp`, and payload.
- Test periods: `100 us`, `1 ms`, `10 ms`, `100 ms`.

Sink behavior:

- Pops one message or a small bounded batch.
- Records enqueue-to-pop latency.

Configurations:

- ThreadPerBlock default.
- ThreadPerBlock with adaptive sleep.
- FixedThreadPool with 2 workers.
- FixedThreadPool with 4 workers.
- FixedThreadPool with pinning when supported.

Expected result:

- This shows whether adaptive sleep is useful or harmful for sparse messages.
- If adaptive sleep adds millisecond-level tail latency, it is not suitable for
  this use case.

### 2. CPU-Contended Message Pipeline

Purpose: determine whether unrelated hot blocks delay message delivery.

Graph:

```text
PeriodicMessageSource -> MessageSink

BusySource0 -> BusyCopy0 -> BusySink0
BusySource1 -> BusyCopy1 -> BusySink1
...
```

Variables:

- Number of busy pipelines: `0`, `1`, `2`, `4`, `8`.
- Busy block work per procedure: `none`, `small memcpy`, `heavy compute`.
- Message period: `1 ms`.

Expected result:

- The default thread-per-block scheduler should have very low idle wake latency
  but can saturate cores when many blocks spin.
- This test shows if that saturation increases message p99/max latency.

### 3. Slow Terminal Sink

Purpose: model a blocking socket, slow file, slow hardware sink, or downstream
process that stops consuming.

Graph:

```text
BurstMessageSource -> Queue -> SlowSink
```

Slow sink modes:

- Sleeps `0 us`, `100 us`, `1 ms`, `10 ms` per item.
- Periodically stalls for `100 ms`.
- Optionally consumes only one item per procedure.

Policies to compare:

- Current FIFO preservation with backpressure.
- Drop newest when output queue is full.
- Drop oldest when output queue is full.
- Keep latest only.
- Stale TTL drop before sink, e.g. drop if item age exceeds `ttl_us`.

Expected result:

- FIFO preservation should have the highest delivery count but worst max age
  under stall.
- Keep-latest and TTL-drop should bound age at the cost of drops.

### 4. Idle Contract Audit

Purpose: verify blocks return "no work" when they did no work.

Test shape:

- Build a flowgraph of idle sources/sinks.
- Run for a fixed interval.
- Count procedure calls and CPU use.

Cases to detect:

- A block returns `Empty{}` while doing zero reads/writes.
- A block returns `Empty{}` on `EAGAIN`, timeout, empty socket, or empty input.

Expected result:

- Blocks that do no work should return a no-progress result.
- If this is not true, the scheduler cannot reliably distinguish progress from
  spin.

## Candidate Framework Changes

Each change below should be tested independently. Do not bundle them before the
benchmarks show value.

### A. Add `Error::NoWork`

Hypothesis:

The scheduler needs a clear non-error no-progress return. Today blocks overload
`NotEnoughSamples`, `NotEnoughSpace`, and sometimes `Empty{}`.

API sketch:

```cpp
enum class Error {
    ...
    NoWork,
    NotEnoughSamples,
    NotEnoughSpace,
    ...
};
```

Test:

- Run the idle contract audit before and after.
- Confirm idle blocks no longer count as successful progress.
- Measure CPU reduction and message latency impact.

Acceptance:

- Idle CPU decreases.
- Message p99 latency does not regress.
- Blocks that actually moved data still report success.

### B. Replace Adaptive Sleep Boolean With Idle Policy

Hypothesis:

`adaptive_sleep` is too coarse. Low-latency applications need explicit control
over spin/yield/sleep behavior.

API sketch:

```cpp
enum class IdlePolicy {
    BusySpin,
    SpinThenYield,
    Relax,
    AdaptiveSleep
};

struct FlowGraphConfig {
    IdlePolicy idle_policy = IdlePolicy::BusySpin;
    size_t spin_iterations_before_yield = 64;
    size_t idle_sleep_us = 1;
    ...
};
```

Test:

- Run low-rate and CPU-contended message benchmarks.
- Compare `BusySpin`, `SpinThenYield`, `Relax`, and current adaptive sleep.

Acceptance:

- A low-latency preset should keep p99 message latency low without saturating a
  full core per idle block.
- Adaptive sleep should be retained only if it wins a clear use case.

### C. Add Freshness Channel Policies

Hypothesis:

Some channels should preserve every item; others should preserve freshness.
This should be a channel-level capability, not custom logic in every block.

API sketches:

Lower-risk additive methods that respect the current SPSC ownership model:

```cpp
bool try_push_drop_newest(const T& v);  // producer-side only
size_t pop_latest(T& v);                // consumer-side only, drains stale items
size_t drain_to_latest(T& v);           // same idea, explicit name
```

Harder policies that do not fit the current SPSC queue for free:

```cpp
enum class FullPolicy {
    Fail,
    DropNewest,
    DropOldest,
    OverwriteOldest
};

template <typename T, size_t N = 0, FullPolicy Policy = FullPolicy::Fail>
struct Channel;
```

The hard part is `DropOldest` / `OverwriteOldest` when initiated by the
producer. Cler's queue is single-producer/single-consumer. The producer owns the
write index; the consumer owns the read index. A producer-side drop-oldest
operation advances the consumer-owned read index, which violates the current
queue invariant unless the queue is redesigned with CAS/coordination or the
consumer cooperates.

Possible safe designs:

```cpp
// Consumer-side freshness adapter.
LatestOnlyBlock<T> latest;

// Producer-side fail/drop-newest policy only.
Channel<T, 0, FullPolicy::DropNewest> ch;

// Future queue type with explicit overwrite semantics and changed invariants.
OverwriteSPSCChannel<T> ch;
```

Test:

- Run slow terminal sink benchmark.
- Compare FIFO, drop-newest, consumer-side keep-latest, and TTL-drop first.
- Test producer-side drop-oldest / overwrite-oldest only after choosing a queue
  design that preserves thread-safety.

Acceptance:

- Freshness policies bound `max_enqueue_to_pop_us` under sink stalls.
- Drop counters match expected losses.
- Existing FIFO behavior remains unchanged by default.
- SPSC ownership rules are preserved or explicitly replaced by a tested queue
  design.

### D. Add Channel Instrumentation

Hypothesis:

Framework users need cheap visibility into queue pressure and latency.

API sketch:

```cpp
struct ChannelStats {
    size_t high_watermark;
    size_t dropped_newest;
    size_t dropped_oldest;
    size_t failed_pushes;
};
```

Optional latency wrapper for message types:

```cpp
template <typename T>
struct TimedItem {
    T value;
    uint64_t enqueue_ticks;
};
```

Test:

- Verify stats in unit tests.
- Use stats in all latency benchmarks.

Acceptance:

- Stats add negligible overhead when disabled.
- Stats expose enough information to diagnose backlog without app-specific
  probes.

### E. Add Block Priority Or Critical Blocks

Hypothesis:

Terminal sinks and hardware sources may need different scheduling treatment than
bulk transform blocks.

API sketch:

```cpp
struct BlockScheduleHint {
    int priority = 0;
    bool pin = false;
    size_t core = 0;
};
```

Possible integration:

- Pass hints into `BlockRunner`.
- Or provide `FlowGraphConfig` arrays indexed by block order.

Test:

- CPU-contended message pipeline with sink marked high priority.
- Compare p99/max latency with and without priority.

Acceptance:

- Priority improves tail latency under contention.
- It does not materially damage throughput in normal pipelines.

## Suggested Implementation Order

1. Add benchmarks only, no scheduler changes.
2. Fix block contract violations where idle blocks return `Empty{}`.
3. Add `Error::NoWork` if the audit shows the current error vocabulary is too
   ambiguous.
4. Add explicit idle policies and benchmark them.
5. Add additive freshness methods to `Channel`.
6. Add channel stats/drop counters.
7. Consider block priority only after the simpler changes are measured.

## Benchmark Executable Proposal

Add:

```text
performance/perf_scheduler_latency.cpp
```

Output format should be line-oriented and easy to diff:

```text
case=low_rate scheduler=thread_per_block idle=busy period_us=1000 p50_us=...
case=contended busy_pipes=4 scheduler=thread_per_block idle=busy p99_us=...
case=slow_sink policy=fifo stall_ms=100 max_age_us=... drops=...
```

Avoid pretty tables as the primary output. Tables are nice for humans but harder
to compare in CI.

## Regression Gates

Once the benchmark exists, use loose CI thresholds first:

- No correctness failures.
- No unbounded queue growth in bounded tests.
- No stale-age regression above an agreed multiple of baseline.
- No throughput regression above an agreed percentage for pure DSP pipelines.

Do not make p99 latency a strict CI gate immediately. Tail latency on shared
CI machines is noisy. Use local runs on a controlled machine for decisions, then
turn stable comparisons into CI gates later.

## Open Questions

- Should Cler continue to default to `ThreadPerBlock`, or should desktop
  examples use an explicit config?
- Should `Empty{}` mean "work happened", or only "no fatal error happened"?
  The scheduler currently assumes the former.
- Should freshness be a channel property, a block property, or an adapter block?
- Should desktop socket blocks be nonblocking by default?
- Is adaptive sleep worth keeping as a named preset once explicit idle policies
  exist?

## Initial Recommendation

Start with benchmarks and the idle contract audit. If idle blocks return
`Empty{}` while doing no work, scheduler experiments will be noisy and hard to
interpret.

After that, prioritize freshness channel methods over sophisticated scheduler
changes. If the application cannot tolerate stale messages, queue policy is the
direct fix; scheduler tuning only reduces the probability of stale backlog.
