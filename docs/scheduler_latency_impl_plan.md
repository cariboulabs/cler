# Scheduler Latency — Implementation Plan

Companion to `scheduler_latency_stress_plan.md`. That doc says *what* and *why*.
This doc says *where in the code*, *in what order*, and *how to know each step is
done*. Branch: `scheduler-latency-stress`.

## Ground Truth (verified against source)

- `FlowGraphConfig` — `include/cler.hpp:181`. Defaults match the stress plan:
  `ThreadPerBlock`, `adaptive_sleep=false`, `max_calls_per_tick=4`,
  `collect_detailed_stats=false`, `pin_workers=false`, `num_workers=4`.
- `Error` enum — `include/cler.hpp:27`. No `NoWork` today.
- Scheduler logic — `handle_adaptive_sleep()` `cler.hpp:297` (early-returns when
  `adaptive_sleep=false`); ThreadPerBlock loop `~cler.hpp:350-434`; FixedThreadPool
  loop `~cler.hpp:600-700` (calls `TaskPolicy::relax()` on no-work, `~:644`).
- `Empty{}` → `did_work=true` (`cler.hpp:410-416`). This is the contract crux.
- Channel = SPSC queue — `include/cler_spsc-queue.hpp`. `push()` busy-waits
  (`:268`), `try_push()` returns false when full (`:299`). No watermark / drop /
  age fields.
- Task policy — `include/task_policies/cler_desktop_tpolicy.hpp`: `relax()`
  (spin 64 + `sleep_us(1)`), `pin_to_core()`. Affinity via `cler_platform.hpp`.
- Config presets — `include/cler_utils.hpp:65+` (`embedded_optimized`,
  `desktop_performance`, `thread_per_block_adaptive_sleep`).
- Perf harness pattern — `performance/perf_simple_linear_flow.cpp`: `BlockBase`
  subclasses, `make_desktop_flowgraph(BlockRunner(...))`, `run_for(duration,
  config)`, `fg.stats()`. CMake: `performance/CMakeLists.txt`, gated by
  `CLER_BUILD_PERFORMANCE`.
- Tests — gtest, `tests/spsc-queue/`, `tests/desktop_blocks/`, gated by
  `CLER_BUILD_TESTS`.

The framework is header-only under `include/`. Every change here is to a header
or a new benchmark/test `.cpp`. No library rebuild surface beyond that.

## Guiding rule

Order is benchmark-first, then contract fix, then *measured* feature additions.
Each candidate change (A–E) lands behind its own benchmark evidence. Do not
bundle. Default behavior of existing DSP pipelines must not change unless a flag
opts in.

---

## Phase 0 — Benchmark scaffold (no framework change)

Goal: a runnable `perf_scheduler_latency` that measures the current framework, so
every later change has a before/after.

Deliverables:

1. `performance/perf_scheduler_latency.cpp`. Reuse the `perf_simple_linear_flow`
   structure. New blocks:
   - `PeriodicMessageSource<T>` — emits one item per `period_us`, item carries
     `{uint64_t sequence, steady_clock::time_point enqueue_ts, payload}`. Uses
     `steady_clock` for pacing; returns the no-progress error when not yet time
     to emit (see Phase 1 — must NOT return `Empty{}` while idle).
   - `MessageSink<T>` — pops 1 or small bounded batch, records
     `now - enqueue_ts` into a latency histogram.
   - `BusySource/BusyCopy/BusySink` — for the contended case; work knob:
     `none | small memcpy | heavy compute`.
   - `BurstMessageSource` + `SlowSink` — for the slow-sink case; sink sleep modes
     `0/100us/1ms/10ms`, periodic `100ms` stall.
2. Latency accounting: local histogram (e.g. fixed buckets or a sorted vector at
   end of run), compute p50/p90/p99/max. `steady_clock` only; no wall-clock in
   the measured path.
3. Line-oriented output exactly as the stress plan specifies
   (`case=... key=val ...`), one line per config. No tables as primary output.
4. `case` dispatch driven by argv so CI / local can run a single case.
5. Wire into `performance/CMakeLists.txt`:
   ```cmake
   add_executable(perf_scheduler_latency perf_scheduler_latency.cpp)
   target_link_libraries(perf_scheduler_latency cler::cler_desktop_blocks)
   ```

Cases to implement (matches stress plan benchmarks 1–3):
- `low_rate`: periods `100us/1ms/10ms/100ms` × {TPB default, TPB adaptive,
  FixedThreadPool 2/4, FTP pinned}.
- `contended`: busy pipes `0/1/2/4/8` × work `{none,memcpy,heavy}`, msg period 1ms.
- `slow_sink`: sink modes × policy (initially only `fifo` — others arrive in
  Phase 4).

Done when: builds under `-DCLER_BUILD_PERFORMANCE=ON`, runs all three cases on
current `main` behavior, emits diffable lines. Commit the baseline numbers to the
PR description (not a CI gate yet).

---

## Phase 1 — Idle contract audit + fixes

Goal: prove (or disprove) that idle blocks return a no-progress result, not
`Empty{}`. This must precede scheduler experiments — `Empty{}`-while-idle makes
every latency number noisy (`did_work=true` keeps the worker hot).

Deliverables:

1. `tests/desktop_blocks/test_idle_contract.cpp` (gtest): build a flowgraph of
   idle sources/sinks, run a fixed interval with `collect_detailed_stats=true`,
   assert `successful_procedures` stays near zero for blocks that did no I/O.
   Add to `tests/desktop_blocks/CMakeLists.txt`.
2. Audit existing desktop blocks (`include/desktop_blocks/` or wherever the
   shipped blocks live) for the two failure shapes:
   - returns `Empty{}` with zero reads/writes,
   - returns `Empty{}` on EAGAIN / timeout / empty socket / empty input.
   Fix each to return `NotEnoughSamples` / `NotEnoughSpace` (today's no-progress
   vocabulary). Record the list of offenders in the PR.

Done when: idle audit test passes; identified blocks fixed; `perf_scheduler_latency
low_rate` CPU drops for the idle-heavy config vs Phase 0 baseline.

Note: this is the step that decides whether Phase 2 (`Error::NoWork`) is even
needed. If the existing `NotEnough*` vocabulary is sufficient and unambiguous
after the audit, Phase 2 may be skipped — that is an explicit decision point.

---

## Phase 2 — `Error::NoWork` (conditional on Phase 1 finding) — SKIPPED

DECISION (after Phase 1 audit): **skipped.** All 11 violating blocks had a
natural `NotEnoughSamples` / `NotEnoughSpace` meaning for their idle path; none
needed a generic "no concept of samples but nothing to do" signal. The existing
no-progress vocabulary was sufficient and unambiguous, so adding `Error::NoWork`
would be churn without benefit. Revisit only if a future block genuinely has no
sample/space semantics yet must signal idle.

Original rationale retained below for reference.

Only if the audit shows the `NotEnough*` vocabulary is genuinely ambiguous
(e.g. a block that has no concept of samples/space still needs to say "nothing to
do").

Deliverables:

1. Add `NoWork` to `Error` (`cler.hpp:27`).
2. Treat it identically to `NotEnough*` in both schedulers: route to the
   no-progress branch (ThreadPerBlock `~:399`, FixedThreadPool `~:696`). Single
   shared predicate `is_no_progress(err)` to avoid drift between the two loops.
3. Migrate audited blocks to `NoWork` where semantically clearer.

Done when: idle audit still passes; `low_rate` p99 does not regress vs Phase 1;
blocks that moved data still report success. Scope warning: this is a repo-wide
contract touch, not a local add — keep the migration list reviewable.

---

## Phase 3 — Idle policy (replace `adaptive_sleep` outright)

Goal: explicit spin/yield/sleep control. **Hard replace** — `adaptive_sleep` bool
is removed, not retained. Breaking API change is accepted: if a policy is cleaner
and faster, we do not carry the old field.

Deliverables:

1. `enum class IdlePolicy { BusySpin, SpinThenYield, Relax, AdaptiveSleep }` in
   `cler.hpp`. Add to `FlowGraphConfig`: `idle_policy`,
   `spin_iterations_before_yield=64`, `idle_sleep_us=1`. The
   `adaptive_sleep_multiplier/_max_us/_fail_threshold` fields stay (now read only
   when `idle_policy==AdaptiveSleep`).
2. Delete the `adaptive_sleep` bool. Default `idle_policy=BusySpin` reproduces
   today's TPB default behavior. `handle_adaptive_sleep()` (`cler.hpp:297`)
   becomes `handle_idle(block_idx, did_work)` dispatching on `idle_policy`:
   `Relax`→`TaskPolicy::relax()`, `SpinThenYield`→spin then
   `std::this_thread::yield()`, `AdaptiveSleep`→current growth logic,
   `BusySpin`→no-op.
3. Migrate every call site (clean break — no shim). Scoped set:
   - `include/cler.hpp` — definition + both scheduler loops.
   - `include/cler_utils.hpp` — `thread_per_block_adaptive_sleep()` preset → set
     `idle_policy=AdaptiveSleep`; other presets set explicit policy.
   - `include/cler_desktop_utils.hpp` — refs updated.
   - `performance/perf_simple_linear_flow.cpp`,
     `performance/perf_fanout_workloads.cpp` — `*.adaptive_sleep = true` →
     `*.idle_policy = IdlePolicy::AdaptiveSleep`.
   - `desktop_examples/{cariboulite_recorder,polyphase_channelizer,cariboulite_spectrum}.cpp`
     — same substitution.
   - `ai-bringup.md` — doc reference.
4. New preset `low_latency()` in `cler_utils.hpp` (BusySpin or SpinThenYield).

Test: `perf_scheduler_latency low_rate` + `contended` across all four policies.
Full build of examples + perf to catch every dropped-field compile error (the
compiler is the migration checklist here).

Done when: BusySpin/SpinThenYield keep p99 low without one-full-core-per-idle-block
where avoidable; adaptive sleep's tail-latency cost is quantified; default DSP
throughput (`perf_simple_linear_flow`) unchanged within noise; all examples +
perf + tests compile against the new field.

---

## Phase 4 — Freshness channel methods (additive, SPSC-safe only)

Goal: bound message age under sink stall. Respect SPSC ownership — this is the
trap the stress plan now calls out.

Safe set (implement these):
- `bool try_push_drop_newest(const T&)` — producer-side; if full, drop the
  incoming item, bump a counter. Pure producer state.
- `size_t pop_latest(T&)` / `drain_to_latest(T&)` — consumer-side; drain all but
  the newest, return count dropped. Pure consumer state.

Add to `cler_spsc-queue.hpp` and expose through `ChannelBase<T>`. Do NOT add
producer-initiated `drop_oldest`/`overwrite_oldest` to the SPSC queue — it mutates
the consumer-owned read index and breaks the invariant. Defer those to a separate
`OverwriteSPSCChannel<T>` type with its own tested design (out of scope here;
note it as future work).

TTL-drop: implement as consumer-side check using the `TimedItem` enqueue
timestamp (Phase 5 wrapper), not as a queue mutation.

Test: `slow_sink` case gains policies `drop_newest`, `keep_latest`, `ttl`. Compare
vs `fifo`. Assert FIFO unchanged by default; freshness policies bound
`max_enqueue_to_pop_us`; drop counts match expectations.

Done when: acceptance bullets in stress plan §C met for the safe subset; SPSC
tests (`tests/spsc-queue/`) still green; new methods covered by unit tests.

---

## Phase 5 — Channel instrumentation

Deliverables:
- `struct ChannelStats { size_t high_watermark, dropped_newest, dropped_oldest,
  failed_pushes; }`. Updated only when a compile-time/flag stats mode is on; zero
  cost when disabled (mirror `collect_detailed_stats` pattern).
- `template<typename T> struct TimedItem { T value; uint64_t enqueue_ticks; };`
  for latency measurement and TTL.

Test: unit tests assert counters; wire stats into all `perf_scheduler_latency`
output lines (`channel_high_watermark`, `dropped_*`, `stale_dropped`).

Done when: stats add negligible overhead disabled (verify with
`perf_simple_linear_flow` throughput unchanged); benchmarks emit the stat fields.

---

## Phase 6 — Block priority / pinning hints (last, only if needed)

Only after 0–5 are measured. `struct BlockScheduleHint { int priority; bool pin;
size_t core; }`. Integrate via `BlockRunner` or a `FlowGraphConfig` array indexed
by block order. Note today pinning is FixedThreadPool-only (`cler.hpp:615`);
ThreadPerBlock never pins. Realistic OS priority needs RT scheduling/privileges —
document the limitation rather than over-promise.

Test: `contended` with sink marked high priority vs not. Done when tail latency
improves under contention without materially hurting normal throughput.

---

## Regression gates (loose first)

Per stress plan: no correctness failures; no unbounded queue growth in bounded
tests; no stale-age regression beyond agreed multiple; no DSP throughput
regression beyond agreed %. p99 is NOT a strict CI gate initially — shared-runner
tail noise. Decisions come from local runs on a controlled machine
(`performance/cpugov.sh` for governor setup); promote stable comparisons to CI
gates later.

## Suggested PR slicing

One PR per phase, each independently reviewable and revertible:
1. PR: benchmark scaffold + baseline numbers.
2. PR: idle audit test + block fixes.
3. PR (maybe): `Error::NoWork` + migration.
4. PR: `IdlePolicy`.
5. PR: freshness methods (safe subset).
6. PR: channel stats + `TimedItem`.
7. PR: schedule hints.

Phases 3–6 are independent given Phase 0–1; can reorder by which benchmark result
is most compelling. Phase 2 gates only on Phase 1's finding.
