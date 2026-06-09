# Cler Scheduler / Idle-CPU Work — Plan for Review

Authoritative, self-contained plan for the scheduler-latency / idle-CPU effort on
branch `scheduler-latency-stress`. Companion `scheduler_latency_stress_plan.md`
is the original problem note; **this document supersedes it** where they differ
(notably: the freshness/drop direction is cut — see Decisions).

Audience: external reviewer approving direction before further implementation.

---

## TL;DR

- **Decision: Cler is lossless.** Never drop / overwrite / reorder samples. The
  freshness/drop direction from the original note is cut. Slow-consumer answer is
  backpressure (already built) or more cores — never dropping.
- **Deployment regime that drives priorities:** `blocks >> cores`, idle CPU
  matters, avoid CPU churn. This means `FixedThreadPool` is the relevant
  scheduler (a thread per block is infeasible when blocks >> cores).
- **Done & pushed:** Phase 0 (latency/CPU/throughput benchmark) and Phase 1
  (idle-contract audit + fix of 11 shipped blocks, with tests). Both lossless,
  both foundational.
- **Key finding (code defect, reviewer-confirmed):** the `FixedThreadPool` worker
  loop **busy-spins worker cores unconditionally** — `get_next_block` auto-resets
  so the inner loop never exits and the intended `relax()` is dead code.
- **Two corrections discovered while implementing (see CORRECTION + Discovery
  log):** (a) a *second* defect — `run_fixed_thread_pool` silently falls back to
  ThreadPerBlock when `num_workers >= blocks` (`cler.hpp:590`); (b) consequently
  the Phase 0 `fixed_pool_*` numbers were **mislabeled** — they ran ThreadPerBlock,
  so FixedThreadPool was never actually measured. The busy-spin finding stands on
  code reading + review but is **empirically unvalidated** pending a
  `blocks > workers` benchmark.
- **Current work:** Phase 3a fix (sweep-once + `rewind`) is applied in the working
  tree, **uncommitted and unvalidated**. Next concrete step: add a
  `blocks > workers` idle benchmark and measure idle `cpu_pct` before/after.
- **Proposed remaining:** Phase 3 worker-loop fix + `IdlePolicy`; Phase 4
  backpressure instrumentation; Phase 5 lossless contention-aware run order;
  Phase 6 priority/pinning. All lossless.

**Status: direction reviewed & approved** by two independent reviews (D1 softened,
Phase 3 split 3a/3b, per-worker backoff, `Relax`/`BusySpin` defaults, Phase 5
opt-in — see "Reviewer sign-off"). Implementation of 3a is in progress and blocked
on building a benchmark that actually exercises FixedThreadPool.

---

## Decisions

### D1 — Core channels are lossless; dropping is out of scope for this effort

Core/default `Channel<T>` is lossless FIFO. This scheduler/idle-CPU effort will
**not** add dropping, overwriting, reordering, or freshness semantics. Rationale:
the domain is sample-stream DSP / sensor measurement (data integrated downstream,
e.g. by estimators), where dropping a sample injects error; samples are not
interchangeable, so a stale backlog is **not** solved by discarding it.

This is a scope boundary, not a permanent ban. If Cler later needs latest-wins /
control-state data (e.g. stale vision frames or old GNSS corrections that can be
worse than skipped), it must be a **separate, explicit, opt-in type** — never a
default-channel capability, so measurements can never be dropped by accident.
That is a future decision, not part of this effort.

Consequences for this effort:
- **Out of scope:** `drop_oldest` / `keep_latest` / `drop_newest` / TTL-drop /
  `OverwriteChannel` / freshness counters / `TimedItem`-for-TTL. (Prototyped on
  this branch, then reverted — see git history.)
- Slow consumer → **backpressure** (`Channel::push` busy-waits;
  `Channel::try_push` returns false when full) or faster consumer / more cores.
- Scheduler work stays lossless: deciding *which ready block to run next*
  (run order) is fine; *dropping* to meet a deadline is not.

### D2 — Deployment regime

`blocks >> cores`, idle CPU matters, minimize CPU churn. Therefore:
- `FixedThreadPool` (static worker pool) is the scheduler of interest;
  `ThreadPerBlock` (one OS thread per block) is infeasible at this scale.
- The default `ThreadPerBlock` + busy-spin path, while it shows excellent latency
  for cores ≥ blocks, is not the target and is not optimized for here.

### D3 — `Error::NoWork` skipped

The Phase 1 audit found every idle block had a natural `NotEnoughSamples` /
`NotEnoughSpace` meaning. The existing no-progress vocabulary is sufficient;
adding `Error::NoWork` would be churn without benefit.

---

## Verified current behavior (file:line)

All confirmed against source on this branch.

- `FlowGraphConfig` defaults (`cler.hpp:182`+): `scheduler = ThreadPerBlock`,
  `adaptive_sleep = false` (`:190`), `max_calls_per_tick = 4`,
  `collect_detailed_stats = false`, `num_workers = 4`, `pin_workers = false`.
- `Error` enum (`cler.hpp:27`): no `NoWork`; `NotEnoughSamples` / `NotEnoughSpace`
  / `NotEnoughSpaceOrSamples` are the no-progress signals.
- `Empty{}` is treated as progress: ThreadPerBlock sets `did_work_in_batch = true`
  on a non-error return (`cler.hpp:414`), keeping the worker hot.
- `handle_adaptive_sleep()` (`cler.hpp:297`) early-returns when
  `adaptive_sleep == false`, so the default does no intentional sleeping.
- Channels are SPSC lock-free queues. `push` busy-waits until space
  (`cler_spsc-queue.hpp:268`); `try_push` fails when full (`:299`);
  `commit_read` (`:553`) advances the consumer-owned read index in O(1).
- Two schedulers: `ThreadPerBlock` (default) and `FixedThreadPool`. Worker
  pinning exists but only in `FixedThreadPool` (`cler.hpp:616`).
- Presets in `cler_utils.hpp:65`+ (`embedded_optimized`, `desktop_performance`,
  `thread_per_block_adaptive_sleep`).

### KEY FINDING — FixedThreadPool busy-spins idle cores

`FixedThreadPoolScheduler` statically partitions blocks into contiguous groups,
one group per worker (`cler.hpp:531`+). The worker loop (`cler.hpp:619`+):

```cpp
while (get_next_block(worker_id, block_idx)) {     // (A)
    bool block_did_work = execute_block_at_index(block_idx, config);
    did_work |= block_did_work;
}
if (!did_work) TaskPolicy::relax();                // (B) intended idle backoff
```

`get_next_block` (`cler.hpp:554`) auto-resets its queue and returns the first
block again when exhausted, so for any worker owning ≥1 block it **always returns
true**. Therefore loop (A) never terminates and the `relax()` at (B) is
**unreachable**. A worker sweeps its blocks round-robin forever, calling
`procedure()` on every idle block, pinning its core at 100% regardless of load.

Status of this finding: the **code defect is real and reviewer-confirmed** (two
independent reviews), but it is **not yet empirically validated** — see the
correction below.

> **CORRECTION (discovered while implementing Phase 3a).** The Phase 0
> `fixed_pool_*` benchmark rows did **not** actually run FixedThreadPool. A second
> defect, the silent fallback in `run_fixed_thread_pool` (`cler.hpp:590`):
> ```cpp
> if (num_workers >= _N) { run_thread_per_block(config); }  // more workers than blocks
> ```
> means a graph with `blocks <= num_workers` runs ThreadPerBlock instead. The
> benchmark's `low_rate` graph has only 2 blocks, so `fixed_pool_2`/`fixed_pool_4`
> (workers ≥ 2) silently fell back. Proven with a stderr marker: zero FTP workers
> started. Therefore every "FixedThreadPool" `cpu_pct=200` number in Phase 0 was
> actually ThreadPerBlock busy-spinning, and the FTP idle path was never exercised.
>
> Consequences: (1) the busy-spin claim still holds by code reading + review, but
> must be confirmed by a benchmark with **`blocks > workers`** (the target
> `blocks >> cores` regime — exactly when FTP engages); (2) the fallback itself is
> a footgun — users who set `num_workers >= blocks` silently get ThreadPerBlock,
> not the pool they asked for. Worth a warning/log at minimum.

(`ThreadPerBlock` separately busy-spins by design when `adaptive_sleep=false`,
which is correct for its latency-first use but not the target regime.)

---

## Evidence from the Phase 0 benchmark

`performance/perf_scheduler_latency.cpp`, line-oriented output, three cases.
Representative numbers (28-core dev box, indicative not gated):

`low_rate` (PeriodicSource → Sink, **2 blocks**), enqueue-to-pop latency + CPU:
- ThreadPerBlock busy-spin: p50 ~0.14us, p99 sub-us, **cpu_pct=200** at all rates.
- ThreadPerBlock adaptive: p50 70us–3.5ms (worse tail), **cpu_pct ~0–11** (cheap).
- ~~FixedThreadPool: cpu_pct=200 even when idle~~ — **INVALID**: with only 2 blocks
  these configs fell back to ThreadPerBlock (`num_workers >= _N`, see CORRECTION
  above). FixedThreadPool was never measured; a `blocks > workers` benchmark is
  required and is the immediate next step.

`contended` (1ms message pipeline + N busy DSP pipelines, ThreadPerBlock):
- message p99 stays sub-us until `busy_pipes=8` saturates cores, then p99 jumps
  to ~1.4–2.2ms, max ~4–5ms. Tail latency under core contention is real.

`slow_sink` (burst source → bounded queue → slow sink, FIFO):
- 100us sink → every item ages to p50 ~162ms; 1ms → ~1.1s; 10ms → ~3s. All
  delivered (lossless). This characterizes backpressure-bound age; under D1 the
  fix is a faster consumer / backpressure, not dropping.

Takeaways: (1) latency vs CPU is the real idle tradeoff and must be measured on
both axes; (2) FixedThreadPool wastes idle cores today; (3) contention inflates
tail latency when blocks >> cores.

---

## Completed work (done & pushed)

### Phase 0 — Benchmark scaffold (lossless, no framework change)

`performance/perf_scheduler_latency.cpp` measures enqueue-to-pop latency
(p50/p90/p99/max via steady_clock), throughput, and **CPU cores used**
(`getrusage`, `cpu_pct`; 200 = two full cores). Three cases: `low_rate`,
`contended`, `slow_sink`. Line-oriented, diffable output. Also fixed the
`performance/` CMake targets to link the real `cler::desktop_blocks` alias (they
referenced a nonexistent target and failed to configure).

### Phase 1 — Idle-contract audit + fixes (lossless)

`tests/desktop_blocks/test_idle_contract.cpp` asserts blocks return a no-progress
error when they move zero data, not `Empty{}` (which the scheduler reads as
"work done"). Audit found and fixed **11 shipped blocks**: `sinks/sink_null`,
`sink_file`, `sink_audio`; `sources/source_file`, `source_audio_file`,
`source_cariboulite`; `udp/source_udp`, `udp/sink_udp`; `utils/fanout`,
`throughput`; `ezgmsk/ezgmsk_mod`. Notable: `sink_null` measures net input-size
delta so a callback that drains the channel itself still counts as work;
`source_udp`'s EAGAIN/empty-socket paths previously returned `Empty{}` *before*
flushing items received that round (data loss) — now flush-then-report.

Behavior under the default `adaptive_sleep=false` is unchanged (both `Empty` and
`NotEnough*` re-loop); the fix is the **prerequisite** for any non-busy idle
policy and for the FixedThreadPool worker to detect "no work" correctly — which
the Phase 3 fix depends on. All 11 unit-test suites pass.

---

## Proposed remaining work (all lossless)

### Phase 3 — FixedThreadPool idle fix + `IdlePolicy`  ← next, highest value

Split into two PRs (reviewer request) so the logic-defect fix is reviewed apart
from the config redesign.

#### Phase 3a — Minimal sweep-once fix (no config change)

Fix the logic defect only; touch no public config. Make each worker sweep its
owned blocks **once** per pass, then reach the idle handler:

```
worker: forever:
    did_work = false
    for each owned block ONCE:
        did_work |= run(block)
    if !did_work: TaskPolicy::relax()    # now reachable
```

Implementation: stop `get_next_block` auto-resetting and add a `rewind()` the
worker calls at the top of each outer pass, so loop (A) terminates after one
sweep and the existing `relax()` at (B) becomes reachable. With the default
`adaptive_sleep=false`, the per-block `handle_adaptive_sleep()` is a no-op, so
3a's net effect is exactly: idle workers `relax()` once per empty sweep instead
of busy-spinning forever.

**STATUS: code applied, uncommitted, NOT yet validated.** The fix is in the
working tree (`get_next_block` no longer auto-resets; `rewind()` added; worker
calls it per pass). It cannot be validated with the current benchmark because the
`low_rate` graph (2 blocks) falls back to ThreadPerBlock (CORRECTION above) — the
FTP path never runs. **Blocking prerequisite:** add a benchmark case with
`blocks > workers` (e.g. M idle source→sink chains under `num_workers=2`), then
measure idle `cpu_pct` before (git-stash the fix) vs after. Only then commit 3a.

Test/acceptance: in a `blocks > workers` idle scenario, FixedThreadPool
`cpu_pct` drops from pegged (~`num_workers`×100) toward ~0; DSP throughput
(`perf_simple_linear_flow`) unchanged within noise; zero sample loss; all tests
pass. Trivially revertible.

#### Phase 3b — `IdlePolicy` + migrate/remove `adaptive_sleep`

1. **`enum class IdlePolicy { BusySpin, SpinThenYield, Relax, AdaptiveSleep }`**
   governs the between-sweep backoff, applied **per worker** (sweep-level), NOT
   per block:
   - `BusySpin` → immediate re-sweep (latency-first, cores ≥ blocks).
   - `SpinThenYield` → spin then `std::this_thread::yield()`.
   - `Relax` → spin then short sleep after a no-work sweep.
   - `AdaptiveSleep` → grow sleep on consecutive no-work sweeps (low-power; an
     explicit preset, NOT a shipped default — adaptive can hide latency
     surprises). Adaptive state moves from per-block to per-worker.
   - Work done on a sweep → no sleep, immediately sweep again (full throughput).
2. **Move fixed-pool no-progress handling out of `execute_block_at_index_helper`**
   to the worker sweep level. Today `handle_adaptive_sleep()` is called per block
   inside the helper (`cler.hpp:698,715`); a worker owning many idle blocks would
   then sleep once per block. The per-block call must be removed/neutralized in
   the fixed-pool path and replaced by a single per-sweep idle decision.
3. **Defaults (do not converge the two schedulers):**
   - `FixedThreadPool` default `idle_policy = Relax` — restores the code's
     original `relax()` intent and gives low idle CPU for the target regime.
   - `ThreadPerBlock` default `idle_policy = BusySpin` — keep it latency-first;
     do not make both schedulers behave the same.
4. **`adaptive_sleep` bool:** the project owner chose to delete it (no shim).
   Isolated to this PR so it is independently revertible. Open question 4 below
   re-checks whether a deprecation window is wanted instead. Keep
   `adaptive_sleep_multiplier/_max_us/_fail_threshold` (read only when
   `AdaptiveSleep`). Migrate call sites: `cler.hpp` (both schedulers),
   `cler_utils.hpp` presets, `cler_desktop_utils.hpp`,
   `performance/perf_{simple_linear_flow,fanout_workloads}.cpp`,
   `desktop_examples/{cariboulite_recorder,polyphase_channelizer,cariboulite_spectrum}.cpp`,
   `ai-bringup.md`. Add a `low_latency()` (BusySpin/SpinThenYield) and a
   `low_power()` (AdaptiveSleep) preset.

Test: `perf_scheduler_latency low_rate` + `contended` across all four policies,
reading `cpu_pct` as a first-class result. Acceptance: each policy behaves as
specified; default-config behavior unchanged from 3a; zero sample loss; all
examples/perf/tests compile and pass.

### Phase 4 — Backpressure instrumentation (no drop counters)

Since nothing is dropped, the diagnostic is "which channels are full/starved and
for how long." `struct ChannelStats { size_t high_watermark; size_t failed_pushes;
size_t failed_pops; }`, updated only under a stats flag (zero cost when off,
mirroring `collect_detailed_stats`). Wire into benchmark output. Acceptance:
negligible overhead when disabled; surfaces backpressure hot-spots.

### Phase 5 — Lossless contention-aware run order (FixedThreadPool)

The surviving "smart scheduler" idea, scoped and lossless — and **experimental,
config-gated, off by default**. Round-robin stays the default (simple,
predictable); starvation-aware ordering can introduce unfairness or odd latency
coupling, so it is opt-in only. Under core contention (Phase 0 `contended` showed
ms-scale message p99 when busy pipes saturate cores), an opt-in mode lets a
worker pick the **most-starved** ready block instead of strict round-robin — run
order only, **no dropping**. Sample k owned blocks ("power of two choices") and
run the best by `score = input_depth + time_since_last_run + priority −
downstream_full_penalty`. Not applicable to `ThreadPerBlock` (no choice — every
block has a thread). Test on `contended`: round-robin vs sample_k=2/4; track
message p99/max, throughput, CPU; confirm zero loss and no fairness regression.

### Phase 6 — Priority / pinning hints (last)

`BlockScheduleHint { int priority; bool pin; size_t core; }` feeding the Phase 5
score and/or OS thread priority/affinity (pinning today is FixedThreadPool-only,
`cler.hpp:616`; OS priority needs RT privileges — document, don't over-promise).

---

## Explicitly out of scope

Freshness / dropping of any kind (D1). If a future channel genuinely carries
"latest-wins" control/state data, it would be a separate, loudly-named, opt-in
type — never a default channel capability, so measurements can't be dropped by
accident. Not part of this effort.

---

## Discovery log

Chronological record of non-obvious findings, so the plan stays honest.

1. **`Empty{}` = progress (Phase 1).** Idle blocks returning `Empty{}` read as
   "did work"; 11 shipped blocks fixed to report no-progress. Done.
2. **Freshness cut (D1).** Owner decision: Cler is lossless; the entire
   freshness/drop direction was prototyped then reverted.
3. **FixedThreadPool busy-spin (code reading).** `get_next_block` auto-resets →
   inner loop never exits → `relax()` unreachable → workers peg cores even idle.
   Reviewer-confirmed. Phase 3a fix applied (sweep-once + `rewind`).
4. **Silent ThreadPerBlock fallback (`cler.hpp:590`).** `run_fixed_thread_pool`
   runs ThreadPerBlock when `num_workers >= blocks`. Discovered when a stderr
   marker showed **zero FTP workers** started for the `fixed_pool_*` benchmark
   rows — the `low_rate` graph has 2 blocks, workers ≥ 2, so it fell back.
   Implications: (a) Phase 0's FTP `cpu_pct` numbers were ThreadPerBlock,
   mislabeled; (b) FTP only runs at `blocks > workers` (the target regime), which
   the benchmark never set up; (c) the fallback is a usability footgun (asking for
   a pool and silently getting per-block threads) — candidate for a warning/log.
5. **Open validation gap.** Phase 3a is applied but unproven. Must add a
   `blocks > workers` idle benchmark and measure idle `cpu_pct` before/after
   before committing 3a. ← current next step.

---

## Regression gates (loose first)

No correctness failures; **zero sample loss** in every test (lossless mandate);
no unbounded queue growth in bounded tests; no DSP throughput regression beyond an
agreed %. p99 is NOT a strict CI gate initially (shared-runner tail noise);
decisions come from local runs on a controlled machine
(`performance/cpugov.sh`), promoting stable comparisons to CI gates later.

---

## PR slicing

1. Benchmark scaffold + baselines. **(done)**
2. Idle-contract test + 11 block fixes. **(done)**
3a. FixedThreadPool minimal sweep-once fix (no config change).  ← next
3b. `IdlePolicy` + migrate/remove `adaptive_sleep` (breaking).
4. Backpressure instrumentation.
5. Lossless contention-aware run order (FixedThreadPool, experimental/off-by-default).
6. Priority / pinning hints.

3a is the minimal logic-defect fix and ships first. 3b/4 are independent given
0–1. Phase 5 is the main scheduler experiment, opt-in, and depends on the
`contended` benchmark. 3a is the priority for the target regime.

---

## Reviewer sign-off (two independent reviews)

Both reviewers approved the direction and confirmed the `FixedThreadPool`
busy-spin reading as a real logic defect, with Phase 3 as the correct next step.
Resolved per their feedback:

1. **D1 wording** — softened from "lossless forever" to "core channels lossless;
   dropping out of scope for this effort; future latest-wins data = separate
   explicit opt-in type." (R1)
2. **Phase 3 split** into 3a (minimal sweep-once fix, no config change) and 3b
   (`IdlePolicy` + migration). (R2)
3. **Per-worker, not per-block** idle handling; the per-block
   `handle_adaptive_sleep()` in the fixed-pool helper must be neutralized. (R1)
4. **Default policy** = `Relax` for FixedThreadPool, `BusySpin` for
   ThreadPerBlock; `AdaptiveSleep` is an explicit low-power preset, not a default.
   Schedulers do not converge behaviorally. (R2)
5. **Phase 5** is experimental, config-gated, off by default; round-robin stays
   the default. (R2)

Still open for the owner:

- **`adaptive_sleep` deletion vs deprecation:** owner chose delete (no shim);
  both reviewers would keep a one-cycle deprecation *if external API consumers
  exist*. Confirm cler is pre-stable / has no external consumers, else keep
  `adaptive_sleep` deprecated for one transition window in 3b.
