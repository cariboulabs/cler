# Frontier: the four 600 -> 500 kS/s resamplers

Status: the throughput question is **settled and instrumented**, the rational
resampler is **wired in and measured on the air**, and the receiver is still
short of 3.0 MS/s — by much less than before.

## Settled: the receiver did not hold 3.0 MS/s

Measured on the Pluto, `pinned_islands(2)`, srulik on 974 MHz channel 3, 30 s
windows, with throughput counters at `channelizer.in` and each `lora_rx.in`.

| probe | liquid `msresamp` | `RationalResampler<5,6,14>` | required |
|---|---|---|---|
| source alone, `pluto_smoke` | 3.000 MS/s | 3.000 MS/s | 3.0 MS/s |
| `channelizer.in` | **1.513 MS/s** | **2.655 / 2.645 MS/s** | 3.0 MS/s |
| one `lora_rx.in` | 252.2 kS/s | 442.6 / 440.9 kS/s | 500 kS/s |
| `channelizer.in` steady occupancy | 100.0% of cap | 87.6% / 93.8% of cap | — |
| ch2 / ch3 payloads in 30 s | 16 / 16, crc_ok 16 / 16 | 32 / 32, crc_ok 32 / 32 (both reps) | — |
| `RX_CPU_CORES` | 1.660 | 1.829 | 2.0 available |

Two reps for the rational configuration, shown as `rep1 / rep2`.

Both signs in the earlier note were right. The graph ran at a self-consistent
50.4% of rate and the AD9361 was overflowing; the source-only bracket proves the
radio was never the limit. Swapping the four resamplers to the rational bank
gives **1.76x on the whole graph** and doubles the decoded frame rate, to 88.5%
of the required rate.

The per-stage ratio `channelizer.in : lora_rx.in` held at exactly 1/6.00 in
every window in both configurations, which is what makes the counters
trustworthy.

### The 100.6% reading was real backlog, and the denominator was wrong

`hi=82431 cap=81920 (100.6%)` compared the watermark against the *requested*
buffer size. cler rounds a request up, so the true usable capacity is 82431:
the channel was sitting at exactly **100.0%** — completely full, continuously,
in every window. Not a startup artifact. The probes now take `cap` from
`in.space()` on the empty channel and track a separate post-warmup watermark, so
this cannot be misread again.

### How the counters work

No block was inserted. Every drain path in the SPSC queue — `commit_read`,
`readN`, `try_pop` — bumps `reader_.cumulativeReadCount_`, which is atomic, so
the existing 200 ms watermark probe in `echo_ground_station/src/receiver/main.cpp`
reads it for free and divides by wall time over a window that starts after a 3 s
warmup. A pass-through `ThroughputBlock` would have cost a thread and a
full-rate memcpy on a box that is already CPU-starved — it would have perturbed
the number being measured. Validate on the desktop file path first: the same
probes are live in `run_file`.

## The replacement, now wired in

`desktop_blocks/resamplers/rational_resampler.hpp` holds both the kernel
`RationalResampler<INTERP, DECIM, TAPS_PER_PHASE>` and the block
`RationalResamplerBlock<INTERP, DECIM, TAPS_PER_PHASE>`. Zero heap, all
`std::array`. The block is a straight dbf-in/dbf-out `procedure()`; it caps the
input by `((write_space - 1) * DECIM) / INTERP` rather than paying for an exact
`outputs_for()` scan, which costs at most one output slot per call.

Kernel measurements (`pluto_sdr/apps/pluto_resamp_bench.cpp`), 5/6, 14
taps/phase, 80 dB, flat across batch 509 / 4096 / 16384:

```
  liquid msresamp_crcf   1.65 MS/s
  RationalResampler      6.27 MS/s      3.81x   (x86: 7.5x)
  output count exact: 50000 from 60000

frequency response, dB relative to DC:
  tone kHz     100    200    240    250    260    300
  ours        -0.0   -0.9   -4.4   -6.0   -8.0  -17.3
  liquid      -0.0   -0.6   -3.4   -4.8   -6.5  -14.6
```

### Correctness, since parity with liquid is impossible

`msresamp` is a different algorithm (fractional-delay bank with interpolation),
so there is no sample-for-sample reference. What is checked instead:

- `tests/desktop_blocks/test_resampler_blocks.cpp` —
  `RationalResamplerBlockExactCountAndBatchContinuity` pushes a 60000-sample
  stream through the block in 509 / 4096 / 16384-sample batches and requires the
  output to equal, sample for sample, one whole-stream call to the bare kernel,
  at exactly 50000 outputs. That is the batch-boundary continuity check.
- `RationalResamplerBlockFrequencyResponse` — 100 kHz passes within 1 dB of DC,
  300 kHz is more than 12 dB down.
- On the air: ch2 and ch3 decode 32 payloads with `crc_ok = 32` in 30 s, up from
  16/16 with liquid.
- `echo_ground_station` ctest, all 13 pass, including
  `test_adjacent_channel_isolation` and `test_step6_bin_replay`.

### Two traps, still true

- **`msresamp`'s 256 phases do not make it sharper.** Transition width comes
  from the span in input samples — 14 taps per phase in both. The phase count
  only buys fractional-delay resolution, which a rational ratio does not need.
- **`msresamp_crcf_get_num_output()` is a stub** in our vendored liquid
  (`resamp.proto.c:298` logs "not implemented" and returns 0), so the
  zero-copy-wrapper idea for the liquid path is dead.

## What is still open

**The receiver is at ~2.65 of 3.0 MS/s, using 1.829 of 2 cores.** Cost per
delivered sample fell from 1.097 to 0.691 cores per MS/s, but the graph is now
close to core-bound rather than starved: a naive linear extrapolation puts 3.0
MS/s at ~2.07 cores, just past what the box has. That extrapolation is
arithmetic, not a measurement — treat it as a reason to measure, not as a
conclusion.

The resamplers are no longer the dominant cost, so the next measurement has to
find what is. Do not guess it from
the CPU table — `Success%` and the per-island `CPU %` in the execution report do
not attribute cost to a block, and every conclusion in this project reached by
arithmetic ahead of a measurement has been wrong. Bracket it: a kernel-only
number for `clora`'s demodulator at 500 kS/s, and the already-known
kernel-only numbers for the channelizer (13.3 MS/s) and the rational resampler
(6.27 MS/s), against the whole-graph total.

`-ffast-math` is the other untried lever: the Pluto toolchain does not pass it,
so both the channelizer fold and the rational resampler subfilter are scalar on
the A9 today, with an estimated 2-4x on those dot products. See the NEON note in
`AGENTS.md` section 9.

## Also unresolved: 600 -> 500 has no guard band

The LoRa signal is 500 kHz wide and the output rate is 500 kS/s, so it fills the
output band to Nyquist exactly. Both resamplers are ~6 dB down at 250 kHz and
alias beyond it. That is inherent to the conversion, not to either
implementation, and it is the same no-guard-band problem as the 600 kHz channel
spacing (see the adjacent-channel duplicate finding in
`echo_ground_station/tests/test_adjacent_channel_isolation.cpp`).

Folding 5/6 into the channelizer would delete all four resamplers, but a
critically-sampled bank ties channel spacing to output rate (both Fs/M), so
500 kS/s outputs force a 500 kHz grid — and the 600 kHz grid is deployed on the
air and cannot move. The grid-preserving version is a rational bank: spacing
advance 5, decimation 6, folding the 5/6 into the filterbank as one pass. That
is now a smaller prize than it was: the resamplers cost far less than they did.
