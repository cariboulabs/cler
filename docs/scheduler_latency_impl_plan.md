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
- **Phase 3a validated then REVERTED.** Added the `ftp_idle` benchmark
  (blocks > workers), which confirmed the busy-spin defect (idle FTP = 200% CPU)
  AND showed the naive sweep-once+`relax()` fix collapses under-load throughput by
  up to 97% (it sleeps on transient pipeline starvation). cler.hpp is back to the
  original busy-spin; only the benchmark was kept/committed.
- **Phase 3 is now a design fork** (Discovery log #6/#7): busy-spin is load-bearing
  for inter-stage handoff because SPSC has no notify-on-push. Reducing idle CPU
  without killing throughput needs a **global-activity-aware backoff** (recommended
  Option B) or event-driven wakeups (Option C) — not a minimal per-worker change.
- **Proposed remaining:** Phase 3 (design fork, below); Phase 4 backpressure
  instrumentation; Phase 5 lossless contention-aware run order; Phase 6
  priority/pinning. All lossless.

**Status: Phase 3 decided, implementation pending.** Owner decisions: **delete
adaptive sleep** (measurably harmful, −98.9% FTP throughput); pursue **Option B**
global-activity backoff (do not burn idle CPU); **FixedThreadPool only**. Option B
is *principled* (attacks adaptive's exact failure cause — local vs global idle
signal) but **not yet proven**; it must pass the two-axis test (idle CPU drops AND
under-load throughput within noise) before committing, else escalate to Option C
(event-driven) or stop at A (busy-spin). This re-review of Option B is the point
of the current doc — the "minimal sweep-once" both reviewers green-lit was shown
insufficient. See Phase 3 for the full argument.

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

### D4 — Delete adaptive sleep

Adaptive sleep is removed, not kept as an `IdlePolicy` option. Measured harmful:
`FixedThreadPool (with adaptive sleep)` throughput is 16.4 MS, −98.9% vs the
1240–1821 MS busy-spin baseline (Discovery log #8), because it sleeps on transient
per-block starvation that is constant under healthy pipeline load. It adds config
surface and per-block atomic state for negative value. Breaking change, no shim
(repo is pre-stable). Its role — cutting idle CPU — is taken over by the global
idle-backoff in Phase 3, which uses the correct (graph-global) signal.

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

### Phase 3 — Delete adaptive sleep + global-activity idle backoff (Option B)

**Decided** (owner): (1) **delete adaptive sleep** outright — it is measurably
harmful (Discovery log #8: −98.9% FTP throughput) and nobody should enable it;
(2) pursue **Option B** (global-activity backoff) to cut idle CPU, because the
owner does not want FTP burning cores when the graph is idle; (3) **FixedThreadPool
only** — that is the `blocks ≫ cores` regime; ThreadPerBlock keeps busy-spin.

Two prior attempts were tried and reverted (Discovery log #6): the naive
sweep-once + `relax()`, and the pre-existing adaptive sleep — both collapse
under-load throughput (38 MS and 16 MS vs a 1240–1821 MS busy-spin baseline).
Phase 3 is the principled fix that explains and avoids that failure.

#### Why Option B should beat adaptive sleep — the core argument

Both sleep when idle. The difference is the **signal that triggers sleep**:

- **Adaptive sleep asks "did *I* (this block) fail?"** — a *local* signal. In a
  pipeline a downstream block's input is empty for a few µs between batches even
  while the pipeline is flowing; adaptive counts that as consecutive fails and
  sleeps (~50µs), upstream backs up, handoff dies → throughput collapse. It cannot
  distinguish "I'm transiently starved but the graph is busy" from "the graph is
  idle." Transient starvation is *constant* under healthy pipeline load, so
  adaptive misfires constantly.
- **Option B asks "did *anyone* work recently?"** — a *global* signal. A worker
  whose sweep came up empty checks graph-wide activity; if any block worked in the
  last few µs it keeps spinning (fast handoff preserved) and sleeps only when the
  *whole graph* goes quiet. The signal is false constantly under load (good — stay
  hot) and true only when truly idle (good — sleep).

One line: **adaptive asks "did I fail?", B asks "did anyone work?"** The first is
true all the time under load; the second only when the graph is genuinely idle.

#### Mechanism (contention-free)

Per-worker activity slots. Each worker owns one cache-line-aligned
`std::atomic<uint64_t> last_active[worker]` and writes **only its own slot**, at
most **once per sweep** (not per block) — uncontended, same cost class as
adaptive's per-block atomic. A worker that just completed a no-work sweep *reads*
all N slots (read-mostly, off the hot path, only at backoff frequency): if every
slot is stale beyond the quiet-threshold, the graph is globally idle → sleep
(escalating); otherwise spin/yield. Active workers never read; idle workers never
touch the hot path.

**Rejected:** a single shared `std::atomic` epoch `fetch_add`-ed on every block
run — that hot cache line bouncing across all cores would be *worse* than adaptive
on the contention axis and throttle the throughput B is meant to preserve.

Still requires the **sweep-once loop fix** (so workers actually reach the backoff;
the part of the reverted prototype that was correct): stop `get_next_block`
auto-resetting, add `rewind()`, call it per outer pass.

#### The one tradeoff the owner accepted

"Don't burn CPU when idle" ⇒ workers sleep when the graph is quiet ⇒ the first
item after a quiet period waits up to **one sleep-quantum** before a worker wakes.
This idle→active wake latency is unavoidable without event-driven wakeups
(Option C). The sleep quantum *is* both the idle-CPU floor and the wake latency
(e.g. 200µs sleep → ~0 idle CPU, ≤200µs wake delay). Owner OK with sub-ms.

#### Correctness condition (where B holds / fails)

B is correct only when the timescales separate:
`handoff gap under load (µs)  <  quiet-threshold  <  idle inter-arrival (ms)`.
For the target regime (µs DSP handoff, ms-apart sparse sensors) the gap is wide,
so B should land. If a stage *legitimately* idles longer than the threshold during
active operation, B mistakes it for global-idle and sleeps → the adaptive failure
returns. The threshold must sit in that gap; **measurement decides.**

#### Honest status — principled, not proven

B attacks adaptive's exact failure cause, but is **not proven** until it passes
the same two-axis test that killed every prior attempt:
- `ftp_idle`: idle FTP `cpu_pct` must drop substantially (from 200).
- `perf_simple_linear_flow`: FTP throughput must stay within noise of the
  busy-spin baseline (1240–1821 MS).
If B fails the throughput axis the same way, escalate to **Option C** (event-driven
notify-on-push — exact wakeups, no threshold guessing, but a much larger change to
the lock-free channel) or stop at Option A (accept busy-spin).

#### Build order

1. **Delete adaptive sleep.** Remove `adaptive_sleep` bool +
   `adaptive_sleep_multiplier/_max_us/_fail_threshold`, the per-block
   `consecutive_fails`/`current_adaptive_sleep_us` atomics, `handle_adaptive_sleep()`,
   the `thread_per_block_adaptive_sleep()` preset, and all call sites
   (`cler.hpp`, `cler_utils.hpp`, `cler_desktop_utils.hpp`,
   `performance/perf_{simple_linear_flow,fanout_workloads}.cpp`,
   `desktop_examples/{cariboulite_recorder,polyphase_channelizer,cariboulite_spectrum}.cpp`,
   `ai-bringup.md`). Breaking change, no shim (owner's call; pre-stable repo).
2. **Sweep-once loop fix** (FTP worker reaches the backoff).
3. **Per-worker activity slots + global-quiet backoff** (spin → sleep on
   threshold; FTP only). Optionally expose quiet-threshold / sleep-quantum as
   `FlowGraphConfig` knobs.
4. **Validate both axes** (above). Commit only if both pass.

Acceptance: idle FTP `cpu_pct` drops substantially; `perf_simple_linear_flow` FTP
throughput within noise of busy-spin baseline; zero sample loss; all tests pass.

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
5. **Validation gap closed — added `ftp_idle` benchmark** (16 Trickle→Drain
   chains = 32 blocks, 2 workers; FTP engages since 32 > 2). Confirmed the
   busy-spin defect empirically: idle FixedThreadPool pegs `cpu_pct=200`.
6. **Naive Phase 3a fix REVERTED — it collapses under-load throughput.**
   Before/after on `perf_simple_linear_flow` (6-block linear pipeline under FTP):
   | idle backoff | under-load throughput | idle cpu |
   | --- | --- | --- |
   | busy-spin (original) | 1240–1821 MS | 200% |
   | sweep-once + `relax()` | 38–500 MS (−97% at 4 workers) | 12% |
   | sweep-once + `yield()` | 1318–1834 MS | 200% |
   The busy-spin is **load-bearing**: a worker owning a downstream stage finds its
   input momentarily empty while upstream is still producing, and `relax()`'s
   ~50µs sleep on that transient starvation destroys pipeline handoff (worse with
   more workers = more split points). `yield()` keeps throughput but does not
   deschedule on an idle multicore, so it saves no idle CPU.
7. **Root cause / design fork.** SPSC channels have **no notify-on-push**, so a
   consumer can only poll: poll fast = CPU, poll slow = handoff latency =
   throughput collapse. Per-worker "consecutive idle sweeps" backoff does NOT
   help — a downstream worker legitimately sees many empty sweeps while the
   pipeline is active. The real signal is **global**: "has the graph done any work
   recently," not per-worker idleness. This makes Phase 3 a genuine design choice
   (see reframed Phase 3 below), not a minimal fix.
8. **The existing per-block adaptive sleep has the SAME flaw — measured.**
   `perf_simple_linear_flow`'s `FixedThreadPool (with adaptive sleep)` row =
   **16.4 MS, −98.9%** vs the 1240–1821 MS busy-spin baseline — *worse* than the
   naive `relax()` fix (38 MS). Reason is identical: adaptive sleep counts
   `consecutive_fails` **per block**, so a transiently-starved downstream block
   sleeps even while the pipeline is active. So "the adaptive sleep we already
   have" does not solve this; enabled on a FTP pipeline it is the worst option on
   throughput. Per-block and per-worker signals are both wrong; only a
   cross-worker (global) activity signal works — see Option B below and its
   contention caveat.

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
3. `ftp_idle` benchmark (exercises FixedThreadPool). **(done)**
4. Delete adaptive sleep (breaking, D4).  ← next
5. Sweep-once loop fix + global-activity idle backoff (Option B), validated
   two-axis. Hold if the throughput axis fails → escalate to Option C or stop.
6. Backpressure instrumentation.
7. Lossless contention-aware run order (FixedThreadPool, experimental/off-by-default).
8. Priority / pinning hints.

PR 4 (delete adaptive sleep) is independent and can land immediately — it is a
pure simplification (D4). PR 5 is the real idle-CPU fix and the one that must pass
the two-axis test before it commits. PRs 6–8 follow.

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

**Superseded by measurement.** Item 2 above (split into a minimal 3a sweep-once
fix) was the reviewers' plan based on the data then available. The `ftp_idle`
benchmark since showed that minimal fix collapses throughput (−97%), and the
existing adaptive sleep is worse (−98.9%). So the plan changed (Discovery log
#6–#8): adaptive sleep is **deleted** (D4), and the idle-CPU fix is the
**global-activity backoff (Option B)** — see Phase 3. The reviewers' caution
about defaults / not-converging schedulers still applies (B is FTP-only;
ThreadPerBlock keeps busy-spin).

Resolved owner decisions:
- **`adaptive_sleep`: delete, no shim** — repo is pre-stable, and it is
  measurably harmful (−98.9%), so a deprecation window is not warranted.

For the re-reviewer (this is the point of the current revision):
- Does the **local-vs-global signal** argument (why Option B should beat adaptive
  sleep) hold up?
- Is the **contention-free per-worker-slot** design sound, or is there a hidden
  hot-path cost?
- Is the **quiet-threshold timescale separation** assumption safe for real Cler
  graphs, or are there pipelines whose stages idle longer than the threshold under
  active load (which would reintroduce the collapse)?
- If B fails the throughput axis, is **Option C** (event-driven wakeups) worth the
  cost, or should we settle for **Option A** (busy-spin) and just delete adaptive
  sleep?
