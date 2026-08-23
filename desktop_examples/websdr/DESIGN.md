# websdr — cler receiver in a browser, over ssh

Status: plan, nothing built. 2026-08-23.

## Goal

A cler flowgraph runs natively on a remote box (RPi + SDR, or a bench PC). From a
laptop: `ssh -L 8080:localhost:8080 box`, open `http://localhost:8080`, get a live
receiver — spectrum, waterfall, click-to-tune, demodulated audio, decoded data —
plus source selection, per-source controls, record to SigMF and playback from it.
All DSP stays native on the box; the browser only draws and plays.

Use cases: client deployments (headless box in a rack/field), remote debugging of
an installed receiver, demos without installing anything on the viewer's machine.

## Non-goals (phase 1)

- Auth, TLS, multi-user arbitration — the ssh tunnel is the auth. `--bind 0.0.0.0`
  for a trusted LAN is the only concession.
- Streaming raw IQ to the browser. Bandwidth and CPU both say no; record on the box.
- WebUSB (SDR plugged into the browser machine). Same client could serve it later;
  not now.
- Replacing the ImGui apps. This is a sibling front-end, not a port.

## Architecture

```
 remote box (native cler)                          laptop
 ┌───────────────────────────────────────────┐     ┌──────────────┐
 │ SourceMux ─┬─> FFT ───────> WebServer ────┼ ws ─┤ index.html   │
 │  hackrf    ├─> AnalogDemod ─┘  ^  |       │     │  waterfall   │
 │  pluto     ├─> decoders (RDS, │  │ ctl    │     │  audio       │
 │  uhd       │   ADS-B, AIS…) ──┘  │        │     │  controls    │
 │  soapy     └─> SigMFRecorder     │        │     │  decoder tabs│
 │  sigmf file (playback) <─────────┘        │     └──────────────┘
 └───────────────────────────────────────────┘
```

Three new pieces; everything else exists.

### 1. `WebServerBlock` (desktop_blocks/web/)

HTTP + WebSocket on one port. Serves the static client (embedded in the binary
via a generated header, so the box needs no files). One WebSocket per browser
tab; N tabs fine, all see the same stream, last control write wins.

Library: uWebSockets via FetchContent (C++17, does HTTP + WS, tiny, fine on RPi).
Fallback if it fights the build: hand-rolled RFC 6455 over a blocking socket
(~300 lines), since we only need text + binary frames, no extensions.

Inputs are cler channels, so the server is an ordinary sink:
- `Channel<SpectrumFrame>` — 1024 × uint8 dB (or configurable N), ~20 fps
- `Channel<float>` audio 48 kHz mono → PCM16 chunks of 20 ms
- `Channel<Text>` decoded lines (RDS, ADS-B JSON, AIS JSON, APRS JSON) — one channel
  type, `{stream, payload}`; browser routes by `stream`
Outputs: control requests go into a lock-free mailbox read by the app thread,
same pattern as `AnalogDemodBlock::set_mode()` (atomic request, applied between
procedure calls). The server never touches DSP state directly.

The block is `may_block`-free: uWS runs its own event loop thread; `procedure()`
just moves ring data into per-socket send buffers with backpressure (drop
spectrum frames when the tab is slow, never audio — audio queue bounded, oldest
dropped with a counter shown in the UI).

### 2. `SourceMux` — runtime source selection

`desktop_examples/spike/spike_source.hpp` already does this (variant over
UHD/HackRF/Pluto with `request_configure`). Promote it to a desktop block
(`desktop_blocks/sources/source_mux.hpp`), add SoapySDR and SigMF-file, and
have it *describe* itself:

```cpp
struct SourceCapabilities {          // what the UI renders, per source
    double freq_min_hz, freq_max_hz;
    std::vector<double> sample_rates;             // discrete list or empty = continuous
    double rate_min_hz, rate_max_hz;
    struct Gain { const char* name; double min, max, step; };  // "lna","vga","amp" / "gain" / "rx"
    std::vector<Gain> gains;
    bool has_bandwidth, has_antenna, has_agc;
    std::vector<std::string> antennas;
    bool is_file;                                 // playback: shows position/loop/speed instead of RF controls
};
```

The client gets this JSON on connect and on source switch and builds the control
panel from it — no per-source UI code in the browser. HackRF shows lna/vga/amp,
Pluto shows one gain + bandwidth, UHD adds antenna, SigMF file shows a transport
bar. The box lists which sources are compiled in and plugged in (`hackrf_device_list`,
iio scan, uhd find, Soapy enumerate) so the dropdown only offers what will open.

Switching sources = tear down the variant, construct the new one, restart the
flowgraph segment. Spike already does it; the FFT/demod chain re-derives rates.
Restart latency of ~1 s is fine.

### 3. Client (desktop_examples/websdr/client/index.html)

One file, vanilla JS, no build step (the repo already carries one heavy web app;
this one must stay trivial to serve from a C++ binary). Canvas waterfall + spectrum
line, WebAudio ring for PCM16, control panel generated from capabilities JSON,
decoder tabs rendered from `Text` streams (RDS as a line, ADS-B/AIS/APRS as tables
with a minimal Leaflet-free SVG map — or no map in phase 1). Click on spectrum =
tune; drag = pan; wheel = zoom into the FFT span. State persists in `localStorage`.

## Protocol (WebSocket)

Binary frames, first byte = type:
- `0x01` spectrum: `u8 type, u32 seq, f64 center_hz, f64 span_hz, u16 n, u8[n] dB`
- `0x02` audio: `u8 type, u32 seq, i16[960]` (20 ms @ 48 k)
Text frames = JSON:
- server→client: `{"t":"hello", caps, state}`, `{"t":"state", ...}` (freq, rate, gains,
  mode, recording, source), `{"t":"text","stream":"rds","data":...}`, `{"t":"error",...}`
- client→server: `{"t":"set", "freq":..}`, `{"t":"source","kind":"hackrf","args":{}}`,
  `{"t":"record","on":true,"path":...}`, `{"t":"play","path":...,"pos":..}`,
  `{"t":"mode":"NBFM"}`
Server echoes `state` after every accepted change so all tabs converge.

## Record / playback

- Record: existing `SigMFRecorderBlock` (scanner already has the Record button).
  Server exposes start/stop, filename, bytes written, free disk. Files land in
  `--record-dir` on the box; the UI lists them.
- Playback: `SourceSigMFBlock` behind SourceMux. Transport: position, loop, pause,
  speed (1x only in phase 1). Center freq/rate come from the SigMF meta, RF
  controls disabled. Demod/decoders work unchanged since they see the same ring.
- Download a recording to the laptop: plain HTTP GET `/recordings/<name>.sigmf-data`
  from the same server — the tunnel carries it.

## Phases

1. **Scanner in a tab** — HackRF only, FFT + waterfall + AnalogDemod audio +
   click-to-tune, WebServerBlock, client. Runs on the x86 box first, then RPi.
   Done when: FM station audible in the laptop browser through `ssh -L`, CPU on
   RPi measured at 2.5 MS/s.
2. **SourceMux + capabilities UI** — Pluto/UHD/Soapy/file, generated control panel,
   device enumeration, source switch at runtime.
3. **Record / playback** — SigMF start/stop, recordings list, playback transport,
   HTTP download.
4. **Decoders as tabs** — RDS text, ADS-B/AIS/APRS tables (JSON feeds from the
   existing blocks), packet_link/modem stats. Map in the browser later.
5. **Ops** — `--bind`, systemd unit for the box, `cler-websdr` CLI with the same
   args as spike, RPi build notes, a gallery entry (screenshot only — it is not a
   wasm demo).

Each phase = a branch, a worker, critic review, merge; the usual.

## Risks / decisions to make

- **uWebSockets on RPi / macOS CI**: needs libuv or its own epoll backend and
  zlib. If it drags, the hand-rolled WS is the escape hatch — decide in phase 1
  after one afternoon.
- **Audio latency**: WebAudio ring + 20 ms chunks + tunnel ≈ 100–300 ms. Fine for
  listening, not for full-duplex. Accepted.
- **Source switch = flowgraph restart**: cler flowgraphs are compile-time; runtime
  source change means the variant trick (spike) with fixed downstream rates, or
  stopping/starting the whole graph. Spike's approach is proven; reuse.
- **CPU on the RPi**: FFT 1024 @ 20 fps is nothing; AnalogDemod at 2–3 MS/s was
  measured fine in the scanner. SigMF recording at 3 MS/s cs8 = 6 MB/s — SD card
  write speed is the limit, USB SSD recommended.
- **Several clients fighting**: last write wins, state broadcast. Good enough
  behind a tunnel. Not revisiting until a client asks.
- **Where the code lives**: blocks in `desktop_blocks/web/` and
  `desktop_blocks/sources/source_mux.hpp`; app + client in `desktop_examples/websdr/`.
  The flowgraph GUI can list it like any example.

## Decisions

- Phase 2 covers every source cler already has: HackRF, SoapySDR, CaribouLite, UHD,
  Pluto, plus SigMF file. (Alon, 2026-08-23.)
- Phase 1 on the x86 bench box, RPi measured before phase 1 is called done.
- Decoder tabs are tables first; a browser map is phase 4+.
