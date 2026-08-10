# Frontier: the last 5% of the Pluto receiver rate

> **Both questions this file was opened for are answered (2026-08-10).** The
> shortfall had an owner: the block-to-island partition. `pinned_islands`
> chose it from cost samples that are noisy by construction, so an identical
> binary and config ran at 3.000, 2.85 or 2.57 MS/s depending on the draw.
> Pinning the partition holds 3.000 in every window, and reception is 100%
> (78/78 frames over 60 s). The corrupted-payload defect was a symptom of
> running in a slow tier and disappears with it; the residual FEC errors are
> ch2 adjacent-channel leakage copies of ch3 frames, 23 dB down.

## Why the partition was a lottery

Weights are `ewma_ns_per_call / ewma_items_per_call`, sampled on 1 call in 61
(`COST_SAMPLE_PERIOD_CALLS`) and only when that call returned `is_ok()`. The
LoRa decode chains are idle between bursts 842 ms apart, so in a 500 ms
calibration window they usually record zero samples and are assigned the
*median* of whatever else was sampled. The DP is deterministic; its input is
not.

Sweeping every cut of the 10-block topological order on the device:

| islands | rate | what it is |
|---|---|---|
| 1/9 .. 4/6 | 2.378 – 2.460 MS/s | |
| 5/5 | 2.577 MS/s | the even split, when calibration never applies |
| 6/4 | 2.853 MS/s | what the cost model picks |
| **7/3** | **3.001 MS/s** | the optimum |
| 8/2 | 2.954 MS/s | |

**The weight is not a load model.** It sums ns/item and discards item rate, so
the channelizer at 3.0 MS/s counts the same as a decoder at 0.5 MS/s. Feeding
it better samples (calibration longer than the traffic period) converges on
6/4 every time — reliably mediocre. Fixing the sampling alone would lock that
in; the objective needs the rate term the two EWMAs already contain.

`PinnedIslandsConfig::manual_islands` now takes the split as block names per
island, and is fatal on unknown, duplicate, missing, empty or
reverse-topological entries.

## Better weights are not enough: the objective is wrong too

Tried and reverted (2026-08-10). Weighting each block by measured utilisation
instead of ns/item — `ns_per_item x items_per_second`, from the cumulative
channel counters, so no hot-path cost — makes the weights physically real and
the partition deterministic. It also picks a *worse* split.

Measured on the Pluto receiver, cores per block:

| block | cores |
|---|---|
| channelizer | 0.341 |
| post_res_ch0..3 | 0.095 each |
| lora_rx_ch0..3 | 0.094 – 0.148 |
| port3_null | 0.009 |

Minimising the max island then chooses 3/7, which balances almost perfectly
(0.47 vs 0.53 cores) and runs at **2.45 MS/s**. The fastest split, 7/3, is the
most *unbalanced* one measured (0.84 vs 0.16 cores) and runs at **3.00**. Rate
rises with imbalance up to 7/3 and falls again at 8/2.

So load balance does not predict throughput on a 2-core target with a
`may_block` source. The likely reason is the source thread: three threads on
two cores, and a nearly idle second island worker leaves a core free to service
the driver promptly, where two evenly loaded workers starve it. Note this
contradicts the note elsewhere that the `may_block` source "does not need a
core reserved for it".

The utilisation weights were kept for `report_partition`, which now prints
cores per block, and the DP was left on ns/item. Fixing the sampling alone
would only make a wrong objective consistent. Until there is an objective that
predicts throughput, name the split with `manual_islands` and find it by
sweeping.

## Ceiling

Replaying a 3 MS/s recording unthrottled, the graph sustains **3.078–3.122
MS/s** at 1.597 cores, about 3% above the 3.000 it must hold. That thin margin
is why only one cut clears the bar and why `channelizer.in` occupancy rides at
its cap even when healthy.

Four channels decoding at once (ch3 fanned into all four chains) costs about
+0.05 core and still holds 3.000 — decode cost tracks sample rate, not traffic.

## Where it stands

`echo_ground_station` receiver on a PlutoSDR (2x Cortex-A9 @ 667 MHz),
`pinned_islands(2)`, srulik transmitting on 974 MHz channel 3.

| probe | liquid `msresamp` | `RationalResampler<5,6,14>` | required |
|---|---|---|---|
| source alone, `pluto_smoke` | 3.000 MS/s | 3.000 MS/s | 3.0 MS/s |
| `channelizer.in`, **production** | not measured | **2.862 MS/s (95.4%)** | 3.0 MS/s |
| `channelizer.in`, under profiler | **1.513 MS/s** | 2.655 / 2.645 / 2.641 MS/s | 3.0 MS/s |
| one `lora_rx.in`, production | not measured | 477.1 kS/s | 500 kS/s |
| occupancy, production | — | 72.1% of cap | — |
| occupancy, under profiler | 100.0% of cap | 87.6% / 93.8% / 89.5% | — |
| `RX_CPU_CORES` under profiler | 1.660 | 1.829 / 1.847 | 2.0 available |
| ch2 / ch3 payloads per 30 s | 16 / 16 | 32 / 32 | — |

**`measure_receiver.sh` sets `CLWB_RECEIVER_BLOCK_STATS=1`, which turns on
`collect_detailed_stats` — per-procedure timing on every block — and that costs
8.1% of throughput.** Every figure this project has published for the receiver
was taken under that profiler, including the ones committed earlier today. The
production number, taken with the same binary and no env var, is 2.862 MS/s:
**95.4% of rate, not 88%.** The remaining gap is ~5%.

The lesson generalises past this repo: the harness was part of the system under
test, and nobody had bracketed it. If a measurement mode exists, measure with it
off at least once.

Cost per delivered sample fell from 1.097 to 0.691 cores per MS/s under the
profiler (no production CPU figure exists — `measure_receiver.sh` is the only
thing that reports cores, and it always enables stats; that is worth fixing).
A linear extrapolation of the profiled numbers put 3.0 MS/s at ~2.07 cores.
**That extrapolation is arithmetic, not a measurement**, and it was built on the
profiled rate, so treat it as void rather than merely uncertain.

## Settled — do not re-derive

- **The receiver never held 3.0 MS/s.** It ran at a self-consistent 50.4% of
  rate with liquid's resamplers, with `channelizer.in` full continuously, so the
  AD9361 was overflowing. Every `RX_CPU_CORES` figure recorded before this was
  taken at an unknown rate and is not comparable to anything.
- **The source was never the limit.** `pluto_smoke local: 975800000 3000000` hits
  3.000 MS/s with a null consumer, in both configurations.
- **The `hi=82431 cap=81920 (100.6%)` reading was real backlog, not a startup
  artifact.** The denominator was wrong: cler rounds a requested buffer size up,
  so the true usable capacity is 82431 and the channel was at exactly 100.0% —
  completely full, in every window. Probes now take `cap` from `in.space()` on
  the empty channel.
- **`crc_ok` in the receiver's report proves nothing.** `build_lora_cfg` sets
  `has_crc = false`, and clora's `frame_decoder.cpp:170` then assigns
  `crc_valid = true` unconditionally. So `crc_ok` equals `payloads` by
  construction and can never fail. Every "crc_ok = 100%" in this project's
  history is vacuous, including the claim that "the DSP chain does not corrupt
  payloads: ch3 payloads=15 crc_ok=15". That claim is **unsupported**, not
  disproven — but see the open defect below, which points the other way.
- **`RationalResamplerBlock<5,6,14>` is correct and is in.** Kernel is 6.27 MS/s
  on the A9 against liquid's 1.65 MS/s (3.81x; 7.5x on x86), exact output count,
  flat across batch 509 / 4096 / 16384, and marginally *sharper* past 200 kHz.
  Pinned by `tests/desktop_blocks/test_resampler_blocks.cpp`: a 60000-sample
  stream cut into varying batches must equal one whole-stream kernel call sample
  for sample at exactly 50000 outputs. Those offline tests are what pin this
  block — the on-air "crc_ok 32/32" that was quoted alongside them is vacuous.
- **`msresamp`'s 256 phases never made it sharper.** Transition width comes from
  the 14-tap span, which both have; the phase count only buys fractional-delay
  resolution a rational ratio does not need.
- **`msresamp_crcf_get_num_output()` is a stub** in the vendored liquid
  (`resamp.proto.c:298` logs "not implemented", returns 0). Any plan that needs
  liquid's output count in advance is dead.

## The method that worked — reuse it for what is left

This is the part worth keeping even after the numbers go stale.

**1. Bracket every question with a source-only and a kernel-only measurement
before touching code.** The whole question turned on one 2-second command:
`pluto_smoke` proving the radio delivers 3.000 MS/s. Without it, "the graph is
short" and "the radio is short" produce identical symptoms and you can spend a
day optimising the wrong half.

**2. Measure throughput at the channels, for free.** Every SPSC drain path —
`commit_read`, `readN`, `try_pop` — bumps `reader_.cumulativeReadCount_`, which
is atomic, so any monitor thread can read it. `echo_ground_station`'s receiver
polls it from the watermark probe that already ran every 200 ms.

Do **not** insert a pass-through counting block. `ThroughputBlock` costs a
thread and a full-rate memcpy; at 3 MS/s complex float that is 24 MB/s of extra
traffic on a box whose whole problem is that it is CPU-starved. It perturbs the
number it is there to measure.

The write-side counter (`producer_thread_cumulative_write_count()`) is a plain
non-atomic `+=`. Reading it from another thread is a data race. Use the read
counter.

**3. Make the instrument prove itself with an invariant.** The chain divides by
6 (M=5 channelizer, then 5/6), so `channelizer.in : lora_rx.in` must be exactly
6.00 regardless of the absolute rate. It read `1/6.00` in every window of every
run, before and after the change. That is what makes the absolute number
believable; a counter that is silently wrong will usually break the ratio first.

**4. Window the rate, and start the window after warmup.** A lifetime average
folds process startup, socket connect and calibration into the denominator and
understates the rate — conveniently in the direction you already suspect. Print
a time series too: one average cannot show decay, and the per-window series is
what proved the backlog was steady rather than transient.

**5. Compute occupancy against real capacity.** `in.space()` on an empty channel,
never the size you asked for.

**6. The execution report cannot find your bottleneck.** With
`CLWB_RECEIVER_BLOCK_STATS=1`, `CPU %` read 99.0-99.8 for *every* block in both
the 50%-of-rate and the 88%-of-rate configuration — it is a per-island figure,
not per-block attribution. `Success%` is the fraction of `procedure()` calls
that returned `Empty{}`, which says how often a block found work, not what it
cost. `AvgTime(us)` is thread lifetime over successful calls and reads in the
millions for idle sinks. Use `block_costs()` after `stop()`, or bracket the
kernel standalone.

**7. Confirm the graph meets the rate before comparing any CPU number.** Stated
in `PLUTO_BENCH_HANDOFF.md` before this session and violated anyway, at the cost
of a whole set of published core counts.

**8. Bracket the harness itself.** `measure_receiver.sh` enables
`CLWB_RECEIVER_BLOCK_STATS=1`; that profiler costs 8.1% of throughput, and every
receiver figure this project ever published was taken with it on. Run once with
the measurement mode off. The same applies to any counter you add: if a probe
cannot be shown to be free, it is part of the system under test.

**9. Check that a counter can fail before trusting it as evidence.** `crc_ok`
read 100% through every run in this project's history because `has_crc = false`
makes it a constant. A pass from a counter that cannot fail is not evidence of
anything. Ask what input would make it read false, and if there isn't one, stop
citing it.

## Four candidate levers for the remaining 5%

Ranked by expected value over cost. None is measured yet. Note the gap is now
~140 kS/s, small enough that lever 4 alone might close it — do not start with
the expensive ones. **The open defect above is more valuable than any of these.**

**1. `-ffast-math` on the ARM build. Cheapest, plausibly sufficient, untried.**
The Pluto toolchain passes `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard` but not
`-ffast-math`, so GCC will not vectorize a floating-point reduction (NEON
single-precision is not fully IEEE, reassociation is barred). Both hot loops in
this chain — the channelizer fold and the rational resampler subfilter — are
scalar on the Pluto today. Estimated headroom on A9 dot products is 2-4x, which
is far more than the 13% needed. Validate LoRa decode on-device afterwards:
`-ffast-math` also changes NaN/Inf behaviour. Note this does *not* help clora,
which is prebuilt separately.

**2. Measure clora. The one unknown large enough to matter.**
Four `lora_rx` blocks each consume 477 kS/s, 1.91 MS/s total. Nothing in this
project has ever measured clora's demodulator standalone. Known kernel numbers
for everything else: channelizer 13.3 MS/s, rational resampler 6.27 MS/s, source
0.196 cores at 3 MS/s. Those account for roughly 0.8 cores of the 1.83 being
spent. The rest is clora plus scheduling overhead, and which of those it is
changes the whole plan. Bracket it the way `pluto_resamp_bench` brackets the
resamplers.

**3. Fold 5/6 into the channelizer, deleting all four resampler blocks.**
Worth less than it was — the resamplers are now ~0.4 cores rather than ~1.5 —
but it removes four blocks, four channels and four island boundaries from a
scheduler running on two cores. A critically-sampled bank ties channel spacing
to output rate (both Fs/M), so 500 kS/s outputs would force a 500 kHz grid, and
**the 600 kHz grid is deployed on the air and cannot move.** The grid-preserving
form is a rational bank: advance 5, decimate 6, folded into the filterbank as
one pass.

**4. Scheduler and buffer tuning. Cheapest, and the gap is now small enough
that this alone might close it.**
`channelizer.in` peaks at 72% occupancy in production and swings between 1 and
27000 samples across windows, so there is a stall pattern left. `pinned_islands(2)` is already the measured-best config for this
box (see `AGENTS.md` section 9) and `calibration_ms` / island boundaries have
not been re-examined since the block mix changed.

## Traps that cost time in this session

- **`serve_out_socket` cannot stand in for echo.** The receiver's
  `SocketSinkBlock` is `SOCK_DGRAM`; `serve_out_socket` is `SOCK_STREAM` and
  handles one client at a time. Point the receiver at it and it blocks forever
  in `block_until_connected()` — which happens *before* `fg.run()`, so the
  process shows 1 thread and 0% CPU and an empty log, looking exactly like a
  dead flowgraph. Stdout is block-buffered when redirected to a file, so nothing
  appears until exit. Use echo, or a 6-line `AF_UNIX`/`SOCK_DGRAM` sink.
- **The desktop file path stops silently at EOF**, leaving `channelizer.in` full
  and every rate at zero. That is not backlog. `run_file` is still the right
  place to validate probe changes before deploying — just give it enough data.
- **Cross-building can ICE under memory pressure.** `firdespm.c` died with
  `internal compiler error: Aborted` with ~1 GB free; identical build was clean
  on retry. Not a code problem.
- **`pkill -f <pattern>` matches your own shell's command line** and kills the
  session. Kill by recorded PID.

## Open defect: payloads reach echo corrupted, on the real channel

This probably outranks the 5% rate gap.

Per 40 s run with one srulik on channel 3 (975.8 MHz), typical:

```
ch2: payloads=35 crc_ok=35 sent=21 sock_bad_hdr=14
ch3: payloads=35 crc_ok=35 sent=24 sock_bad_hdr=11
```

and echo logs, continuously, for the frames that do get through:

```
sr_radio_mac.c:96: MAC: RX message decoding failed with error code:
  Telemetry message FEC decode error, from channel 2, for src_id 0x00AF280C
  ... same, from channel 3 ...
```

What that chain says: clora emits 35 payloads, ~11-14 of them are rejected by
`check_header()` in `SocketSinkBlock` and never leave the receiver, and of the
~24 that do reach echo, Reed-Solomon fails. **ch3 is the channel the srulik is
actually on**, not the adjacent duplicate, so this is not explained by the
accepted adjacent-channel finding — ch2 failing is expected, ch3 failing is not.

Why it went unnoticed: `crc_ok` cannot fail (see above), so the receiver has
reported a clean bill of health throughout. The counter that would have caught
this was disabled by `has_crc = false`.

Where to start, in order:
1. Turn `has_crc` on end to end (srulik TX included) and see whether clora's CRC
   fails on the same frames echo's FEC rejects. That single change converts a
   vacuous counter into the discriminator this needs, and tells you immediately
   whether the bytes are already wrong when clora hands them over.
2. If clora's CRC passes but echo's FEC fails, the corruption is between the
   socket sink and the MAC — check `check_header()` and the DecodedFrame
   framing, not the DSP.
3. Compare against `pluto_decode_one_channel`, which runs radio → decimator →
   clora with no channelizer and no resampler. If its payloads decode cleanly in
   echo and the receiver's do not, the DSP chain is implicated after all.

**Do not add an RSSI or SNR gate** to suppress the ch2 copies — that discards,
and losing a real frame is worse than a duplicate. See
`echo_ground_station/tests/test_adjacent_channel_isolation.cpp`.

## Open observations — not conclusions

- Channelizer port 3 is computed and thrown into a null sink. M=5 with four
  used channels. Whether an M=5 bank can skip one output cheaply is a question,
  not a claim — the DFT across M produces all ports together.
- `ECHO_CPU_CORES` reads 0.000 in `measure_receiver.sh`. The receiver is the
  only measured consumer; echo's own cost is unaccounted for.

## Also unresolved: 600 -> 500 has no guard band

The LoRa signal is 500 kHz wide and the output rate is 500 kS/s, so it fills the
output band to Nyquist exactly. Both resamplers are ~6 dB down at 250 kHz and
alias beyond it. Inherent to the conversion, not to either implementation, and
the same no-guard-band problem as the 600 kHz channel spacing.
