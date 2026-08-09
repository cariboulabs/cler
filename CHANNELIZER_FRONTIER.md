# Frontier: make the polyphase channelizer 3x faster on ARM

One number decides this work. Everything else is context.

```
pluto_chan_bench, PlutoSDR (2x Cortex-A9 @ 667 MHz), M=5, 3.0 MS/s in:

  now:      per_port = 203.6 kS/s   ratio = 0.34
  target:   per_port = 600.0 kS/s   ratio = 1.00
```

Ship when `ratio >= 1.0`. Nothing downstream needs deciding until it moves.

## Why this is the frontier

`echo_ground_station` receives 4 LoRa channels on a Pluto. Measured on-device
(`echo_ground_station/docs/PLUTO_CPU_MEASUREMENT.md`), the full 15-block graph
burns 2.018 of 2 cores with `channelizer.in` pinned at 100% occupancy while
every downstream queue sits at 3-6%. The channelizer starves the whole back
half. It is a single cler block, so it is capped at one core no matter what the
scheduler does.

Scheduler tuning is done and is not the answer: `pinned_islands(2)` already
measured best of five configs (merged to main, see AGENTS.md "Fanout with
Imbalanced Processing"). Two workers on this bench is *worse* than one, because
one block cannot use two cores.

## Why 3x is reachable

At 3 MS/s the channelizer owes roughly:

- filterbank: 5 subfilters x 6 taps = 30 crcf MACs per 5 input samples
  -> 6/sample -> 18 M crcf MACs/s ~= 72 MFLOP/s
- DFT-5: 25 complex mults x 600 k frames/s ~= 90 MFLOP/s

~160 MFLOP/s total. A9 NEON peak is ~2.7 GFLOP/s per core. It currently
processes ~1.0 MS/s per core, i.e. ~53 MFLOP/s of useful math -- **about 2% of
peak**. Closing 3x means reaching ~6%. The gap is implementation, not
arithmetic.

## Where the time goes

`desktop_blocks/channelizers/polyphase_channelizer.hpp` is fine -- it batches
frames and commits once per batch. The cost is inside liquid, one
`firpfbch_crcf_analyzer_execute` per **5 samples** (600 k calls/s):

1. **Naive DFT-5.** liquid selects `LIQUID_FFT_METHOD_DFT` for nfft <= 8
   (`fft_utilities.c:50-52`): 25 complex multiplies (100 real) per frame. A
   Winograd-5 codelet is 10 real multiplies + ~34 adds. The DFT is over half
   the arithmetic, so this alone is worth ~1.8x.
2. **Per-call setup.** Each call pushes 5 samples through `windowcf`
   bookkeeping. Replace with a flat delay line; batch many frames per call so
   subfilter state stays in registers.
3. **Scalar DFT and window handling.** liquid's subfilter dot products already
   use NEON on ARM (`dotprod_crcf.neon.c.o` is in the armhf build); the rest is
   not.

## Measuring

Bench source: `pluto_sdr/apps/pluto_chan_bench.cpp` (links cler + echo's
prebuilt armhf liquid). Build and deploy:

```
cd ~/repos/pluto_sdr/apps && ./build_arm.sh
scp -O build_arm/pluto_chan_bench root@192.168.2.1:/mnt/devel/
```

Run on the device, one worker (more is worse here):

```
nice -n 19 /mnt/devel/pluto_chan_bench 15 974600000 3000000 1
```

Console: the Pluto's USB-gadget console is a `ttyACM*` under VID:PID
`0456:b673` -- **the node number moves between boots**, find it by VID:PID.
Login `root` / `analog`. Full harness notes and three time-wasting traps are in
`pluto_sdr/apps/PLUTO_BENCH_HANDOFF.md`.

## Already ruled out -- do not redo these

- **The four resamplers are not the bottleneck.** They are the largest FIR load
  on paper (~224 MFLOP/s) but their queues sit at 3-6%. Deleting them now buys
  nothing.
- **Scheduler choice.** Five configs measured; `pinned_islands(2)` wins. Done.
- **`may_block` on the Pluto source.** Was missing in echo, now fixed; the
  source thread measures 0.196 cores.
- **The board "hang".** Did not reproduce in 10+ single-config runs. The Pluto
  also died once with nothing running on it (EPROTO on the USB gadget), so
  suspect the cable and supply, not cler.

## Step two, after the number lands

Once the channelizer fits, the four `resamp_crcf` (600->500 kS/s, ~224 MFLOP/s)
become the next wall. Channelizing 3.0 MS/s into **6 x 500 kS/s** deletes all
four: the filterbank still costs 6 MACs per input sample and Winograd-6 is
cheaper than Winograd-5. Cost is channel spacing moving 600 -> 500 kHz, which
requires changing the srulik frequency plan -- confirmed acceptable, both ends
are ours.

Then bump `echo_ground_station`'s cler submodule and re-run
`measure_receiver.sh` on the device.
