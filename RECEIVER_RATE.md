# Holding 3 MS/s on a 2-core Pluto — settled findings

What running the `echo_ground_station` LoRa receiver on a PlutoSDR (2x Cortex-A9
@ 667 MHz) taught us about cler's scheduler, resamplers and probes. Everything
here is measured. Receiver-side numbers and how to reproduce them live in that
project's `docs/RECEPTION_MEASUREMENT.md`.

Status: the rate question is closed. The graph holds 3.000 MS/s in every window
at ~1.53 of 2 cores, and reception is 100%.

## The partition decides throughput, and the cost model picks badly

`pinned_islands` chooses which blocks share which worker from cost samples. On
an identical binary and config that choice made the graph run at 3.000, 2.85 or
2.57 MS/s — decided at startup, stable for the whole run.

**Why it was a lottery.** Weights are `ewma_ns_per_call / ewma_items_per_call`,
sampled on 1 call in 61 (`COST_SAMPLE_PERIOD_CALLS`) and only when that call
returned `is_ok()`. Blocks that are idle between bursts usually record *nothing*
in a 500 ms calibration window and are then assigned the *median* of whatever
else was sampled. The DP is deterministic; its input is noise.

**Why better weights do not fix it.** Weighting each block by measured
utilisation instead — `ns_per_item x items_per_second`, from the cumulative
channel counters, so no hot-path cost — is physically correct and makes the
choice deterministic. It also picks a *worse* split. Measured cores per block:

| block | cores |
|---|---|
| channelizer | 0.341 |
| resampler (each of 4) | 0.095 |
| LoRa decoder (each of 4) | 0.094 – 0.148 |
| null sink | 0.009 |

Minimising the max island then balances almost perfectly, 0.47 against 0.53
cores, and runs at **2.45 MS/s**. The fastest split measured is the most
*unbalanced*, 0.84 against 0.16, at **3.00**. Sweeping every cut of the
10-block topological order:

| islands | rate |
|---|---|
| 1/9 .. 4/6 | 2.378 – 2.460 MS/s |
| 5/5 (even) | 2.577 MS/s |
| 6/4 (what the cost model picks) | 2.853 MS/s |
| **7/3** | **3.001 MS/s** |
| 8/2 | 2.954 MS/s |

So **load balance does not predict throughput** on a 2-core target with a
`may_block` source. The likely mechanism is the source thread: three threads on
two cores, and a nearly idle second island worker leaves a core free to service
the driver promptly, where two evenly loaded workers starve it. This contradicts
the older claim that a `may_block` source needs no core reserved for it.

The DP therefore still uses ns/item — a wrong objective made consistent is worse
than one that occasionally gets lucky. The utilisation weights were kept for
`report_partition`, which prints cores per block. **Until there is an objective
that predicts throughput, name the split with `pinned_islands.islands` and find
it by sweeping.**

## Ceiling and margin

Replaying a 3 MS/s recording unthrottled, the graph sustains **3.078–3.122
MS/s** at 1.597 cores — about 3% above the 3.000 it must hold. That thin margin
is why only one cut of nine clears the bar and why `channelizer.in` occupancy
rides at its cap even when healthy.

Decode cost tracks sample rate, not traffic: four channels decoding at once
costs about +0.05 core over one. But a channel whose channelizer port is routed
to a null sink skips its resampler and decoder entirely, ~0.19 cores each, so
running fewer channels is a real saving — one channel is 0.87 cores against 1.56
for four, and occupancy drops from the cap to near empty.

## Settled — do not re-derive

- **`RationalResamplerBlock<5,6,14>` is correct and is in.** 6.27 MS/s on the A9
  against liquid `msresamp`'s 1.65 (3.81x; 7.5x on x86), exact output count, flat
  across batch 509 / 4096 / 16384, marginally *sharper* past 200 kHz. Pinned by
  `tests/desktop_blocks/test_resampler_blocks.cpp`: a 60000-sample stream cut
  into varying batches must equal one whole-stream kernel call sample for sample
  at exactly 50000 outputs.
- **`msresamp`'s 256 phases never made it sharper.** Transition width comes from
  the 14-tap span, which both have; phase count only buys fractional-delay
  resolution a rational ratio does not need.
- **`msresamp_crcf_get_num_output()` is a stub** in the vendored liquid
  (`resamp.proto.c:298` logs "not implemented", returns 0). Any plan needing
  liquid's output count in advance is dead.
- **Do not measure the graph ceiling through a 4 MS/s file.** The 4->3
  `msresamp` pre-resampler caps that path at 0.919 MS/s and measures itself.
  Record at 3 MS/s and feed the channelizer directly.
- **`crc_ok` in the receiver's report proves nothing.** `build_lora_cfg` sets
  `has_crc = false` and clora's `frame_decoder.cpp:170` then assigns
  `crc_valid = true` unconditionally, so `crc_ok` equals `payloads` by
  construction. Any "crc_ok = 100%" claim is vacuous. Measure reception from the
  transmit cadence instead.
- **Probe capacity must come from `in.space()` on the empty channel.** cler
  rounds a requested buffer size up, so a hardcoded denominator reads >100%.

## Traps that cost time

- **`serve_out_socket` cannot stand in for echo.** The receiver's
  `SocketSinkBlock` is `SOCK_DGRAM`; `serve_out_socket` is `SOCK_STREAM` and
  takes one client. Point the receiver at it and it blocks forever in
  `block_until_connected()`, *before* `fg.run()`, so the process shows 1 thread,
  0% CPU and an empty log — indistinguishable from a dead flowgraph, since
  stdout is block-buffered when redirected.
- **The file path stops silently at EOF**, leaving `channelizer.in` full and
  every rate at zero. That is not backlog.
- **Cross-building can ICE under memory pressure.** `firdespm.c` died with
  `internal compiler error` twice at high `-j`; identical build clean on retry.
  Not a code problem.
- **`pkill -f <pattern>` matches your own shell's command line** and kills the
  session. Kill by recorded PID.
- **Console I/O is not what limits the rate.** It looked that way once; the run
  in question had simply landed in a slow partition tier. Both were changed at
  the same time, which is what made the false attribution easy.

## Still open

- **No guard band at 600 -> 500 kHz.** The LoRa signal is 500 kHz wide and the
  output rate is 500 kS/s, so it fills the band to Nyquist exactly. Both
  resamplers are ~6 dB down at 250 kHz and alias beyond. Inherent to the
  conversion, not to either implementation — the same problem as the 600 kHz
  channel spacing, which is why adjacent channels duplicate each other's frames.
- **Unused channelizer ports are still computed.** The DFT across M produces all
  ports together, so whether an M=5 bank can skip an output cheaply is a
  question, not a claim.
- **The scheduler's objective.** See above: it needs one that predicts
  throughput, not balance.
