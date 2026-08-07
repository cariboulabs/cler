# Cler Scheduler Overhaul — Working Plan

Goal: run real SDR flowgraphs (PlutoSDR RX, ~MSPS rates) on 2-core ARM Cortex-A9 @800MHz. Secondary: desktop gets faster too.

## Ground rules

- **No comments in code.** Self-explanatory names and small functions instead. A name like `spin_then_park_after_zero_progress_pass()` replaces a paragraph. Existing comments in touched code get converted to explicit code or deleted.
- Cler style holds: no throw/try/catch, `cler::Result` for recoverable, `cler::panic` for init failures, no allocation in hot paths, C++17.
- Every phase gates on benchmarks: `perf_simple_linear_flow` + `perf_read_write_techniques` on desktop before/after, sustained-rate Pluto RX flowgraph on target for Phase 2+.
- Workflow: Sonnet writes mechanical/localized changes, Opus writes scheduler-core changes, Fable critiques every chunk before merge. One reviewable chunk per task below.

## Phase 0 — Correctness and idle-cost bugs (no design decisions)

| # | Task | Where | Acceptance |
|---|------|-------|------------|
| 0.1 | `get_next_block()` returns false at end of pass; worker loop resets once per pass | cler.hpp:554, :619 | worker reaches idle path when all blocks blocked; throughput unchanged when saturated |
| 0.2 | Remove sleep/relax from inside block execution; worker backs off only after a full zero-progress pass | cler.hpp:297, :695 | blocked block no longer stalls colocated blocks |
| 0.3 | `relax()` loses the 1µs nanosleep: bounded spin (ARM yield/x86 pause) → capped exponential backoff. `prctl(PR_SET_TIMERSLACK, 1)` on worker start | cler_desktop_tpolicy.hpp:32 | idle worker CPU drops; no 50µs-slack syscalls in hot path |
| 0.4 | Hard bound on num_workers vs storage; clamp + panic on overflow | cler.hpp:525, :574 | no OOB write possible |
| 0.5 | Stop flag: one relaxed load per pass, release store on stop | cler.hpp:619–679 | identical behavior, fewer barriers on ARM |

Model: Sonnet. Fable reviews as one diff.

## Phase 1 — Cost telemetry (scheduler gets data)

| # | Task | Notes |
|---|------|-------|
| 1.1 | Per-block sampled cost: time 1-in-64 procedure calls, EWMA of ns/call; items/call measured via output-channel occupancy delta around the sampled call | replaces lifetime-average stats as cost model; <1% overhead target |
| 1.2 | Expose `BlockCost { ewma_ns_per_call, ewma_items_per_call }` in stats; keep old report fields working | consumed by Phase 2 partitioner |

Model: Sonnet, Opus if the occupancy-delta plumbing through the tuple gets hairy. Fable reviews.

## Phase 2 — Topology + two pinned islands (the big wins)

| # | Task | Notes |
|---|------|-------|
| 2.1 | Topology plumbing, current block syntax preserved: FlowGraph derives edges at init by memory-span containment — each runner's output channel pointer is matched to the block whose object range [block, block+sizeof) contains it (concrete types known via BlockRunner templates). Blocks holding channels in heap containers (vector-based: add, plots, ~4 files) additionally call a typed `BlockBase::register_input(channel)` in their constructor; registered addresses checked before containment. Unresolved output → no edge, init warning names the orphan, partitioner falls back to contiguous split | prerequisite for 2.2–2.4; validate derived edges against cler-validate on desktop_examples |
| 2.2 | `MayBlock` trait on BlockRunner; blocking blocks (Pluto/HackRF/file/UDP sources+sinks) excluded from compute islands, each gets a dedicated thread | tag ~6 blocks |
| 2.3 | Partitioner: topo-sort, cut chain/DAG into `num_workers` islands minimizing max sum(cost) + crossing-bytes penalty, using Phase 1 costs after a calibration window; fallback to contiguous split when costs absent | replaces equal-count split |
| 2.4 | Island execution: blocks run in topological order, run-to-completion per pass, workers pinned (explicit cpu_ids, affinity errors checked) | this is the 2–3× lever |
| 2.5 | Wake discipline: worker parks on futex after N zero-progress passes; producer wakes peer only if peer's parked flag set, at most once per pass | Opus; missed-wake protocol needs care |
| 2.6 | New scheduler exposed as `SchedulerType::PinnedIslands`; ThreadPerBlock stays default desktop, PinnedIslands default when workers ≤ cores ≤ 2 | keep old paths working |

Model: Opus for 2.3–2.5, Sonnet for 2.1–2.2, 2.6. Fable critiques design of 2.3/2.5 before code.

## Phase 3 — Measured-need only

| # | Task | Trigger |
|---|------|---------|
| 3.1 | Lazy dbf peer-index caching in SPSC queue (`read_dbf`/`write_dbf` use cached window until insufficient) | always worth it; Sonnet, 1 day |
| 3.2 | Readiness bitset: skip blocks whose edges say cannot progress | if Phase 2 profiling shows failed-call overhead remains |
| 3.3 | True fusion (compile-time thunk, no SPSC on internal 1:1 edges) | only if queue overhead still dominates after 2.4; weeks of work; decide from data |

## Deployment config (docs, zero code)

- Pluto: `isolcpus=1 nohz_full=1 rcu_nocbs=1`, DSP island pinned to core 1, I/O + Linux on core 0
- `chrt -f` (SCHED_FIFO) for flowgraph process; document up-to-50% CFS penalty
- Chunk sizing guidance: channel quota sized so a pass touches ≤16KB per block (L1D is 32KB on A9)

## Explicitly not doing

Work stealing (2 cores, 1 victim), online rebalancing, priority-heap occupancy scheduling, general task runtime (need ≥10µs tasks to break even; our kernels at L1 chunks are ~3µs).

## Evidence base

GRCon19 Bloessl (fused order 2.75×, SCHED_RR +50%), StreamIt LCTES'05 (2.5× on StrongARM), GR4 (buffer elimination 30× on trivial kernels), qsdr/FOSDEM25 (L1 quanta, pin per core, spin-then-park), Tokio LIFO slot / Go runnext / TBB bypass / HyPer morsels (run consumer next on same core), Unity job system (never wake per item), Task Bench (10µs task floor).

## Phase 2 design spec (2.3–2.6) — implementation contract

### New scheduler type
`SchedulerType::PinnedIslands`. FixedThreadPool stays byte-for-byte behavioral for existing users. PinnedIslands shares the worker/queue machinery but adds: calibration→cost partition, topo execution order, park/wake, affinity on by default.

### Lifecycle
1. Startup: may-block blocks → dedicated threads (as FixedThreadPool). Regular blocks → contiguous topo-order split across workers (fallback partition), workers pinned to cpu ids (config `cpu_id_offset`, default 0; check and report affinity failure via stats, do not abort).
2. Calibration: run normally for `config.calibration_ms` (default 500). Phase 1 EWMAs accumulate.
3. Repartition (once): worker 0 is leader. At first pass boundary past deadline, leader sets `repartition_pending`; every worker parks at its next pass boundary on the partition futex; leader waits for all, computes new partition, rebuilds queues, bumps `partition_epoch`, wakes all. Workers re-read their queue on epoch change. One-shot; no periodic rebalancing.
4. Steady state: each pass executes the worker's blocks in topo order (upstream→downstream), micro-batching per block as today.

### Partition algorithm (leader, cold path)
- Topo order via Kahn over edges(); blocks in cycles or unreachable keep insertion order after sorted ones.
- Block weight = ewma_ns_per_call / max(ewma_items_per_call, 1.0). Zero-sample blocks get median weight.
- W workers: choose W-1 cut points in the topo sequence minimizing max island weight-sum, plus penalty `CROSS_EDGE_PENALTY_NS = 200` per edge crossing a cut (SPSC handoff cost stand-in). Chain length ≤ 256, W ≤ 8: brute force / DP fine on cold path.
- Islands stay contiguous in topo order. v1 limitation accepted: uniform item-rate assumption (resamplers skew weights; revisit with rate signatures later).

### Park/wake protocol (the missed-wake dance, follow exactly)
- Per worker: `atomic<uint32_t> sleep_epoch`, `atomic<bool> parked`.
- Worker with zero-progress passes escalates through existing BackoffState (spin→yield); after `PARK_AFTER_ZERO_PASSES = 4`, it: sets `parked = true` (release), runs ONE more full pass, if that pass made progress → clear parked, continue; else `TaskPolicy::park(sleep_epoch, observed_epoch)`.
- Any worker whose pass made progress: after the pass, scan other workers' `parked` flags (relaxed); for each set: bump that worker's `sleep_epoch`, `TaskPolicy::unpark(...)`, at most one wake attempt per worker per pass. May-block dedicated threads do the same scan after successful procedures (they produce into pooled consumers).
- The extra full pass after setting `parked` closes the race: data committed before the final pass is found by it; data committed after is committed by a producer that already sees `parked==true` and wakes.
- TaskPolicy gains `park(const std::atomic<uint32_t>&, uint32_t expected)` / `unpark(std::atomic<uint32_t>&)`. Desktop: Linux futex syscall (FUTEX_WAIT_PRIVATE/FUTEX_WAKE_PRIVATE); macOS fallback: mutex+condvar per worker. Base policy default: park = escalated backoff sleep (embedded stays syscall-free, semantics preserved).
- Shutdown: stop() bumps every sleep_epoch and unparks all workers before joining.

### Config additions
`calibration_ms = 500`, `cpu_id_offset = 0`, `park_after_zero_passes = 4`. PinnedIslands implies pin_workers unless explicitly disabled.

### Acceptance
- Test: imbalanced synthetic chain (one heavy block) → post-calibration partition puts heavy block alone; assert via a new `partition()` accessor.
- Test: park/wake — bursty source, workers reach parked state (observable counter), no deadlock over 10s run, wake latency sane.
- Stress: existing tests pass under PinnedIslands; perf_simple_linear_flow PinnedIslands ≥ FixedThreadPool.
- Idle CPU: parked flowgraph (throttled source) near-zero CPU vs current busy loop.

### Implementation notes (post-review, landed)
- Park has a 1 ms timeout (futex and condvar paths). Two reasons: timer-driven blocks (throttle) fail passes with no producer to send a wake, and the arm→final-pass→park sequence has a store-buffering race (worker's parked-flag store vs producer's data commit can mutually miss under acq/rel); the timeout bounds both to a ≤1 ms hiccup instead of a fence per producer pass.
- PinnedIslands is opt-in (`flowgraph_config::pinned_islands(n)`), not auto-defaulted on 2-core machines — silently switching existing users' scheduler broke "keep old paths working". Plan row 2.6 superseded.
- Cut objective: max island weight + 200 ns × total crossing edges (global term; per-island folding inverts the isolate-heavy-block incentive). DP exact for chains, heuristic on fan-out.

## Architecture review outcomes (2026-08-07)

Adversarial architecture pass findings and dispositions:
- ACCEPTED, done/queued: delete dormant register_input machinery (no in-tree users, per-block RAM tax); FusedBlock composes kernel functors, not blocks (dead-channel waste); telemetry sampling gated to PinnedIslands and EWMAs stored as atomic<uint64_t> bits (Cortex-M lock-free + static-link safety); adaptive_sleep DELETED entirely (superseded by backoff ladder + futex park; user call 2026-08-07); manual island override in config as the deterministic embedded path (DP fills around it); embedded_optimized() helper updated to current thesis; document CLER_DEFAULT_MAX_WORKERS=2 for small targets.
- CORRECTED CLAIM: FixedThreadPool is NOT byte-for-byte pre-branch behavior — the may_block lane changes thread count and mapping for graphs containing blocking blocks. This is a deliberate behavior improvement, not preservation.
- CLARIFIED SCOPE: "no online rebalancing" bars per-pass migration/stealing; the drift repartition (5s cadence, 20% hysteresis, user-requested for bursty LoRa) is epoch-scale and stands.
- KEPT: park arm→final-pass→futex dance (timeout is the backstop; the dance saves a guaranteed 1ms stall on the common race); span-containment edge derivation (formally unspecified pointer comparison, universally fine on supported targets; typed-registration-only alternative rejected as a worse API).
- QUEUED (post correctness-review): collapse FixedThreadPool worker loop into the islands loop parameterized by idle policy.
- WATCHLIST: drift repartition named most likely 6-month trap (only mechanism rewiring SPSC thread ownership mid-run); mitigation = burst test coverage in the lora-shape bench and repartition_count() observability.
- CONSIDERED, REJECTED: procedure() returning samples-processed — sampled EWMAs already serve partitioning; exact counts only pay for per-call scheduling we don't do; signature change breaks every block. If ever needed: optional SFINAE-detected last_procedure_items() accessor, no signature change.
- FOLLOW-UP FOUND (pre-existing, independent of this branch): plot blocks (plot_timeseries, plot_cspectrum) access internal channels from both procedure() (worker) and render() (GUI thread) — violates SPSC single-reader assumptions; peek_read/size/read_dbf from render() are unsynchronized. Lazy caching was deliberately NOT applied to peek/size paths because of this. Needs its own fix: hand-off ring or GUI-side snapshot buffer.

## NEON findings for the Pluto build (2026-08-07, echo-side TODO)

- Pluto toolchain already passes -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard, but NOT -ffast-math. ARM codegen probe (clang armv7): with -ffast-math all hot loop shapes vectorize to NEON q-register ops including complex MAC; without it, dotprod reductions stay scalar (FP reassociation barred). Today's build is effectively scalar in its hottest loops.
- liquid-dsp ships hand-written NEON dotprod kernels (dotprod_{rrrf,crcf,cccf}.neon.c) but its CMake compiles only the portable scalar versions - the whole SIMD source block is commented out upstream. The resampler and polyphase channelizer bottom out in exactly these dotprods.
- Expected NEON win on A9 dotprods: 2-4x, matching the estimated 3-4x shortfall to 4 MS/s. Likely decisive.
- Echo-side actions (needs ARM toolchain to verify, not shippable blind from this machine): (1) add -ffast-math to pluto-armhf.cmake and validate LoRa decode once on device; (2) preferably patch echo's liquid FetchContent to compile the .neon.c dotprod sources in place of the portable ones (same symbols - replace, not add).
