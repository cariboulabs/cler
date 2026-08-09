# Frontier: the four 600 -> 500 kS/s resamplers

The channelizer is done (`ratio = 1.00`, ~10x kernel, folded into AGENTS.md
section 11). This is what replaced it as the suspect.

## The number that matters is not yet measured

**Does the full `echo_ground_station` receiver keep up at 3.0 MS/s?** Nobody has
checked. `measure_receiver.sh` reports CPU and queue watermarks but no
throughput, and every CPU figure quoted so far was taken without confirming
`msps ~= rate`. `PLUTO_BENCH_HANDOFF.md` warns about exactly this.

Two independent signs say it does **not** keep up:

1. **Frame rate.** Same srulik on 974 MHz channel 3, same afternoon:
   - `pluto_decode_one_channel` (radio -> decimator -> clora, no receiver):
     14 payloads in 11.5 s = **1.21/s**
   - full receiver, `ch3`: 15 payloads in 30 s = **0.50/s**

   The receiver sees under half the frames the direct path does.

2. **Arithmetic.** `pluto_resamp_bench` on the A9 measures liquid's
   `msresamp_crcf` at 5/6 sustaining **1.65 MS/s** of input, single threaded and
   flat across batch sizes 509 / 4096 / 16384. Four resamplers are fed
   4 x 600 kS/s = 2.4 MS/s, which needs **~1.46 cores for the resamplers alone**
   — against a measured whole-receiver total of 1.674 cores including source,
   channelizer, four demodulators and four sinks. Both cannot be true.

The likely reconciliation: the graph runs at a slower self-consistent steady
state, `channelizer.in` really is backed up at its reported 100.6%, and the
AD9361 is overflowing. That reading was previously dismissed as a startup
artifact. It should be re-examined.

**Do this first:** add a throughput counter to the receiver (samples/s at the
channelizer input and at one `lora_rx` input) and confirm whether the graph
holds 3.0 MS/s. Every conclusion below is contingent on that answer.

## The replacement already exists and is measured

`desktop_blocks/resamplers/rational_resampler.hpp` —
`RationalResampler<INTERP, DECIM, TAPS_PER_PHASE>`, zero heap, all `std::array`,
same shape as the channelizer work.

Output `n` needs input `i = floor(n*DECIM/INTERP)` at phase
`p = (n*DECIM) mod INTERP`, so for 5/6 the schedule repeats every 5 outputs per
6 inputs with integer input advances `1,1,1,1,2`. Per output it runs one
subfilter of `TAPS_PER_PHASE` real-by-complex MACs over a contiguous window read
in place from the caller's span, with a `TAPS_PER_PHASE-1` sample carry between
calls. No floating-point phase accumulator, no interpolation between two
fractional filters, no window object, no dot-product dispatch.

Measured by `pluto_sdr/apps/pluto_resamp_bench.cpp`:

```
PlutoSDR Cortex-A9, 5/6, 14 taps/phase, 80 dB:
  liquid msresamp_crcf   1.65 MS/s
  RationalResampler      6.27 MS/s      3.81x
  x86:                                  7.5x

output count exact (50000 from 60000), flat across batch 509/4096/16384

frequency response, dB relative to DC:
  tone kHz     100    200    240    250    260    300
  ours        -0.0   -0.9   -4.4   -6.0   -8.0  -17.3
  liquid      -0.0   -0.6   -3.4   -4.8   -6.5  -14.6
```

Ours is marginally *sharper* everywhere past 200 kHz.

### Two traps in comparing these

- **`msresamp`'s 256 phases do not make it a sharper filter.** Transition width
  is set by the filter's span in input samples — 14 taps per phase in both
  cases. The phase count only buys fractional-delay resolution, which a rational
  ratio does not need. An early version of this note claimed liquid had an
  effective 3584-tap prototype; that is wrong.
- **The audit's "cheap first step" does not exist.** It proposed keeping liquid
  and removing the wrapper's two copies using
  `msresamp_crcf_get_num_output()`. In our vendored liquid that path reaches
  `resamp.proto.c:298`, which is a stub: it calls `liquid_error(LIQUID_EINT,
  "... not implemented")` and returns 0.

### Not parity-testable

Unlike the channelizer, this cannot be pinned to liquid sample-for-sample —
`msresamp` is a different algorithm (fractional-delay bank with interpolation).
Correctness has to be response shape, exact output count, continuity across
batch boundaries, and end-to-end LoRa `crc_ok` rate.

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
advance 5, decimation 6, folding the 5/6 into the filterbank as one pass.

## Order of work

1. Instrument receiver throughput; confirm or refute that it holds 3.0 MS/s.
2. If it does not, wire `RationalResampler<5, 6, 14>` into
   `MultiStageResamplerBlock`'s place behind the same block API and re-measure
   both throughput and `crc_ok` rate per channel.
3. Only then consider the rational channelizer that deletes the resamplers.
