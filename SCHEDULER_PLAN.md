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
