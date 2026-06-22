# History

Notable changes to Cler. Newest first.

## Unreleased

### Added
- **`spike` example** (`desktop_examples/spike.cpp`): a slim, Spike-style spectrum
  analyzer GUI for USRP. Single window with a live control panel (center frequency
  and gain are tunable on the fly; sample rate / span is fixed at startup), an
  oscilloscope-style zero-span trigger view, and a spectrum view for context.
- **`TriggerBlock`** (`desktop_blocks/triggers/trigger_block.hpp`): backpressure-safe
  zero-span capture trigger that renders its own oscilloscope frame.
  - Rising/falling edge with a hysteresis latch that persists across `procedure()`
    call boundaries (no missed or duplicated triggers at the seams).
  - Normal / Single / Auto modes; sample-based holdoff and Auto free-run timeout.
  - Configurable pre-trigger fraction; each trigger repaints one fixed-timebase
    window with the trigger pinned at t=0 (replaces the previous frame).
  - Live, thread-safe reconfiguration via a mutex-guarded config snapshot applied
    by the DSP thread at a safe point (no cross-thread mutation of block state).
  - Display decimation: large windows are drawn as a peak-preserving min/max
    envelope (≤8000 points), so multi-second windows render smoothly. Capture
    buffers are bounded by a memory ceiling (~256 MB) regardless of sample rate.

### Changed
- **`SourceUHDBlock`**: added `request_configure()` for thread-safe live retune.
  Reconfiguration requests are staged and applied by the streaming thread itself,
  so the USRP is never touched concurrently with an in-flight `recv()`.
- **`PowerDetectorBlock`**: converted from per-sample `push`/`pop` to space-gated
  bulk `readN`/`writeN`, so a slow downstream consumer throttles it instead of
  corrupting or dropping samples.
