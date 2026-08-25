# earshot — where things stand (2026-08-25)

Working notes for picking this up cold. `DESIGN.md` is the plan of record; this
file is only "what is done, what is broken, what is next".

## Done and on main

Phases 1-4 of DESIGN.md, plus the architecture-debt round that followed:

- Browser receiver served by the box the radio is plugged into. Devices list,
  generated control panel, double-click tune with the scanner's edge rule and a
  400 kHz IF offset, in-place source switch (`fg.stop → mux.select →
  set_ratio → fg.reset → fg.run`).
- Sources through `SourceMux`: HackRF, Pluto, UHD, CaribouLite, SoapySDR, SigMF
  playback, simulator. A failed connect explains itself on the device row
  (permissions, busy, no receiver in the firmware, unreadable datatype…).
- SigMF record and playback: repeatable `--record-dir`, name preview before you
  start, per-dir cap with oldest-first prune that never touches a live capture,
  recordings dialog with play/download/delete and an scp line.
- Decoders as a set (RDS, APRS, AIS wired; ADS-B declared unavailable with its
  reason). Measured cost hints: rds ~2%, aprs ~2%, ais ~7% of an i7 core.
- UI pass against the cler-fg rules: sectioned panel, ⋯ menus, dialogs, keyboard,
  data-testid throughout, palette converged on the repo tokens, inferno colormap
  with the waterfall auto-ranging to the noise floor.
- Ops: systemd unit with sd_notify watchdog, `/health`, state file, token forced
  off loopback, Origin+Host rebinding check, loopback listens on both families.
- `openwebrx_connector`: hands any cler radio to a real OpenWebRX (verified on
  the Pi driving a CaribouLite, zero-patch route via the `sddc` type).
- Cross-compile to the Pi: `cmake/toolchains/rpi-aarch64.cmake` (needs
  gcc-10-aarch64 + a sysroot) or `docker/Dockerfile.build` under qemu.
- Scheduler: PinnedIslands, measured on the Pi at ~87% of a core vs ~155% for
  ThreadPerBlock at 2.4 MS/s, zero drops either way. `EARSHOT_SCHEDULER` env
  switches it for re-measurement.

## Open, with evidence

1. **macOS: a spectrum frame is read that was never written.** `earshot_integration_test`
   fails ~50% on macOS with `seq=[4,4,…,4,0]` — a frame carrying gen 0 — and the
   Debug job also reports `binary frames shorter than a header: 0,0,0,0`. One
   cause fits both: the tick loop encoded a slot the producer had not committed,
   so `encode_spectrum` refused the garbage `n` and sent nothing. Not reproducible
   on Linux (10/10), and NOT the software-mirror fallback: forcing it with
   `-DCLER_DISABLE_DOUBLY_MAPPED=ON` passes 3/3. Suspect the doubly-mapped path
   on macOS (mach `vm_remap`) or `read_dbf`'s bound on that platform. Start at
   `include/cler_spsc-queue.hpp` read_dbf/commit_read and `WebServer::tick_loop`.
2. **`earshot_e2e_test` is stale.** It drives markup that `e43992c` changed
   (decoders moved out of the ⋯ menu into the Decoded pane) and `.check()`s the
   raw input rather than clicking the label. Plan agreed with Alon: rewrite as
   intent-based helpers ("turn on the rds decoder, wherever that control lives"),
   trim e2e to ~5 journeys (loads+connects, tune+hear, record→list→play, viewer
   cannot control, narrow viewport usable) and push detail down to the
   integration test and the node tests.
3. Recheck macOS `connector_test` after the `SO_NOSIGPIPE` fix (it was failing
   for the real reason that MSG_NOSIGNAL makes send() fail on macOS).

## Next, in rough order

- Phase 5: WAN profile — Opus audio, deflated spectrum rows, fps/N negotiation
  driven by send-queue depth. The `hello` handshake already carries codec lists.
- ADS-B decoder tab: needs a full-rate 1090 MHz magnitude tap and CPR
  aggregation outside the GUI-only aggregator.
- `spike_source.hpp` → `SourceMux` (promised in DESIGN.md phase 3, still not
  done): blocked on SourceMux gaining a live `set_rate()` that restarts the
  device stream without stopping the graph, plus `actual_rate()`.
- Docs refresh in one pass: README showcase row, demo gallery card and
  `desktop_examples/earshot/README.md` with a screenshot of the redesigned UI.
- AIS/APRS still unverified off air (no VHF antenna).

## How this repo is worked

Workers in git worktrees, one branch each, a critic reviewing the branch diff
before merge, fixes back to the same worker. Hardware: HackRF on the desk,
Pluto reachable at `pluto:ip:169.254.12.76`, RPi 4 + CaribouLite at
`pi@raspberrypi-alon.local` running `cler-earshot.service` on :8080 (tunnel with
`ssh -L 8080:127.0.0.1:8080`). Never tune near 930 MHz. No GUI windows on Alon's
desktop; kill by exact name, never `pkill -f` with a self-matching pattern.
