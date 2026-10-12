# Liquid-free BPSK link

Four header-only blocks in `cler::blocks_core`. They build with `CLER_BUILD_BLOCKS_LIQUID=OFF`
and do not allocate in `procedure()`. Together they carry text between a transmitter and a
receiver that **share no clock**: the receiver estimates the carrier offset and the
sample-clock rate offset itself.

```
packets ─► BPSKFramerBlock ─► BPSKModulatorBlock ─► (radio or channel) ─► BPSKReceiverBlock ─► BPSKDeframerBlock ─► packets
```

`linear_modem/LinearDemodulatorBlock` already demodulates BPSK on top of liquid-dsp. These
blocks spell out every synchronisation step in about 600 lines, for readers and for targets
without liquid.

## Frame and packet format

| Barker-13 ×2 | length | payload | CRC-16-CCITT | guard |
|---|---|---|---|---|
| 26 symbols | 1 byte (1..64) | 1..64 bytes | 2 bytes, over length + payload | 8 zero symbols |

- **Bit mapping:** one symbol per bit, bit 0 → +1, bit 1 → −1, bytes MSB first.
- **Packets in the graph:** length-prefixed records `[length][payload]` on a `uint8_t` channel, at the framer input and the deframer output.
- **Lead-in:** the first frame is preceded by 128 symbols of a fixed x⁷+x⁶+1 pattern.
- **Idle fill:** with `idle_fill`, the framer sends that same pattern whenever no packet is waiting. A radio then keeps streaming and the receiver stays locked. The pattern's normalised correlation with the preamble is at most 0.54.

## Modulator

The modulator is a polyphase RRC interpolator: β 0.35, span 10 symbols, `sps` samples per symbol. Its taps have
unit energy, so ±1 symbols give **Es = 1**. That is the same convention as `LinearModulatorBlock`, so
an AWGN stddev of `sqrt(0.5 · 10^(−Es/N0 / 10))` (`bpsk::noise_stddev_for_esn0_db`) means the
same Es/N0 for both modems. TX RRC * RX RRC is Nyquist: ISI below 1e-2 at every other symbol centre.

## Receiver, stage by stage

The receiver works on blocks of `block_size` input samples (default 4096), so its latency is one block.

1. **Coarse CFO.**
   - *Intuition:* squaring BPSK removes the ±1 data and leaves a pure tone at twice the carrier offset.
   - *Formula:* `f̂ = argmax |FFT(x²)| / 2`, with a Hann window and a parabolic fit on log power around the peak.
   - *Range:* |CFO| < fs/4.
   - *Application:* the estimate is applied to the same block it was measured on. Each block's estimate is smoothed into the running one (weight 0.25). Blocks whose x² peak is below 30× the mean bin power are ignored, so silence cannot pull the estimate. An NCO then removes the offset.
2. **Matched filter.** The TX RRC taps, decimated to 2 samples/symbol.
3. **AGC.** Applied to the matched-filter output, so the power measurement sees only the signal band: `P ← P + 1e-3 (|y|² − P)`, `g = sqrt((1 − β/4) / P)`. For random data, 1 − β/4 is the mean RC-pulse power at any timing phase. The AGC therefore puts symbols at amplitude 1, which is what the detector gains below assume.
4. **Timing (Gardner).**
   - *Error:* `e = Re{(y[k] − y[k−1]) · conj(y[k−½])}`. Positive e means sampling late. The detector is decision-free and rotation-invariant, so it runs before carrier recovery.
   - *Interpolation:* a piecewise-parabolic Farrow interpolator (α 0.5) produces samples between the inputs at fraction μ.
   - *Control:* a modulo-1 position steps half a symbol per interpolant, and a PI loop sets the step, `step = 1 − (K1 e + ∫K2 e)`.
   - *Clamp:* the symbol period is limited to ±2 %.
5. **Carrier (Costas).**
   - *Error:* at 1 sps, `z = y e^{−jθ}`, `e = sgn(Re z) · Im z`.
   - *Loop:* PI, `θ += K1 e + ∫K2 e`.
   - *Clamp:* the frequency is limited to ±5 % of Rs.
   - *Ambiguity:* the loop settles at 0 or 180°, and the deframer resolves which.
6. **Lock and quality.**
   - *Lock:* smoothed cos 2φ = (I² − Q²)/(I² + Q²) > 0.7.
   - *EVM:* smoothed |z − sgn(Re z)|².
   - *Clock-rate error:* measured from how far the interpolator advanced over 2048-symbol windows, not from the loop integrator, which carries the loop noise.

**Loop gains** use the Rice formula (`bpsk::pi_gains`): θ = BnTs / (ζ + 1/(4ζ)), d = 1 + 2ζθ + θ²,
K1 = 4ζθ / (d Kp K0), K2 = 4θ² / (d Kp K0). Both loops update once per symbol with K0 = 1.

**Measuring Kp:** Kp is the detector's S-curve slope at zero error. To measure it, average the detector over 20k
noiseless random symbols passed through TX RRC * RX RRC at unit amplitude (β 0.35), sweeping the
timing or phase offset. The results are Gardner Kp = 1.356 per symbol of timing error and Costas Kp = 1.0 per radian.

| Loop | BnTs | ζ | Kp | K1 | K2 |
|---|---|---|---|---|---|
| timing | 0.01 | 1.0 | 1.356 | 0.0232 | 1.86e-4 |
| carrier | 0.01 | 0.707 | 1.0 | 0.0263 | 3.51e-4 |

## Deframer

The deframer correlates the last 26 soft symbols with the preamble, normalised to [0, 1]. When the
metric reaches 0.7, it keeps searching for 16 more symbols and takes the maximum. A window 13 symbols early
lines the second Barker-13 up with the first and can cross 0.7 by itself when data or idle
symbols precede the preamble. The phase of the peak correlation de-rotates the frame, so a
Costas loop that settled at 180° gives the same bits. Its counters are: OK, CRC fail, bad length, detections and inverted frames.

## Measured (simulation, deterministic seeds)

**End-to-end messages** (`tests/desktop_blocks/test_bpsk_blocks.cpp`: sps 8, fs 2 MS/s, Rs 250 kBd, 50 messages of 1–64 bytes):

| Channel | Messages OK |
|---|---|
| clean, Es/N0 20 dB | 50/50 |
| ±50 ppm and ±41 kHz CFO (HackRF + Pluto worst case at 918.5 MHz), Es/N0 15 dB | 50/50 |
| π phase rotation | 20/20 (all via the inverted preamble) |
| CFO up to ±490 kHz (fs/4 = 500 kHz), Es/N0 15 dB | 50/50 |
| clock offset up to 5000 ppm (CFO 41 kHz) | 50/50; 41/50 at 10 000 ppm |
| Es/N0 10 / 9 / 8 / 6 dB (+50 ppm, 41 kHz) | 50 / 48 / 47 / 25 of 50 |

The frame loss at 6–8 dB is close to what BPSK theory predicts for frames of about 300 bits.

**Sync harness**: the same chain, seeds and lock definition as the liquid baseline. Lock time is the first
symbol from which the 256-symbol rolling EVM stays ≤ −15 dB at Es/N0 20 dB. Settings are sps 4 and Rs 250 kBd.

| | BPSKReceiverBlock | LinearDemodulatorBlock (liquid) |
|---|---|---|
| lock time, 0–1000 ppm | 260 symbols | 336–347 symbols |
| clock offset, locks up to | ±10 000 ppm (period clamp ±2 %) | ±20 000 ppm |
| CFO, locks up to | 90 % of Rs (fs/4 = 100 % at sps 4), in 260 symbols | 10 % of Rs (in 56k–94k symbols) |
| BER gap to Q(√(2Eb/N0)), 0–10 dB, 1 and 100 ppm | 0.03–0.31 dB, 0 re-alignments | 0.11–0.57 dB, up to 21 re-alignments |
| steady EVM at Es/N0 20 dB | −19.3 dB | −19.7 dB |
| sps 8 (fs 2 MS/s) at ±1 / ±4 ppm with coupled CFO | locks in 260 symbols, 0 EVM excursions | acquires in 600, 1–4 excursions over −15 dB |

## Run it

```
./bpsk_link --sim                       # GUI: ppm / CFO / Es/N0 sliders, constellations, loop state, messages
./bpsk_link --sim --headless            # one status line per second
./bpsk_link --tx hackrf --freq 918.5e6 --rate 2e6 --gain 0
./bpsk_link --rx pluto --uri ip:192.168.2.1 --freq 918.5e6 --rate 2e6 [--record rx.cf32]
```

## Limits

- **Latency:** one receiver block (4096 samples, 2 ms at 2 MS/s). A transmitter that stops sending samples holds the last frame inside the receiver; use `idle_fill`.
- **Clock offset:** limited to ±2 % by the period clamp. CFO must stay under fs/4.
- **Sample rate:** sps must be even, because the receiver decimates to 2 samples/symbol.
- **Modulation:** BPSK only, with no FEC and at most 64 bytes per frame.
