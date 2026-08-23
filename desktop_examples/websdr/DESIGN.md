# websdr — cler receiver in a browser, over ssh

Status: plan v3 — v2 was three independent critiques (architecture, ops/security,
frontend/protocol); v3 puts SourceMux back over the compiled-in backends, makes
the device list the first screen, and switches sources in place. Nothing built.
2026-08-23.

## Goal

A cler flowgraph runs natively on a remote box (RPi + SDR, or a bench PC). From a
laptop: `ssh -L 8080:localhost:8080 box`, open `http://localhost:8080`, get a live
receiver — spectrum, waterfall, click-to-tune, demodulated audio, decoded data —
plus source selection, per-source controls, record to SigMF and playback from it.
All DSP stays native on the box; the browser only draws and plays.

Use cases: client deployments (headless box in a rack/field), remote debugging of
an installed receiver, demos without installing anything on the viewer's machine.

## Non-goals

- Multi-user arbitration beyond "one controller, N viewers". TLS. Accounts.
- Streaming raw IQ to the browser. Record on the box instead.
- Client-side demodulation (no-sdr's model). cler's value is the native block chain.
- WebUSB (SDR on the browser machine). The same client could serve it later.
- Replacing the ImGui apps. Sibling front-end, not a port.

## Prior art, and what to borrow

- **no-sdr** (Go + SolidJS, MIT): shared server FFT, per-client IQ sub-band with
  browser demod; FFT delta+deflate, audio ADPCM/Opus, ~12 KB/s per client; binary
  type-byte frames server→client, JSON client→server; audio-gated streams; demo
  simulator source; profiles. Borrow: protocol shape, fps caps, sim source, profiles.
- **OpenWebRX / OpenWebRX+** (Python + csdr, AGPL): the UX everyone knows. Ideas only.
- **BrowSDR** (Rust/wasm, AGPL), **wavelet-lab/websdr** (TS, WebUSB): the
  SDR-in-the-browser-machine case. Later WebUSB mode only.
- **SpyServer / SDR++ server / rtl_tcp / SoapyRemote**: IQ to a native client.
  SoapyRemote is the zero-code LAN fallback ("run the ImGui scanner on the laptop
  against a remote SDR") — say so in the README.
- **Libraries**: IXWebSocket (BSD; HTTP + WS, thread-safe `send`, no libuv; one
  FetchContent) — decided, not uWebSockets. libopus (BSD) and zlib for the WAN
  profile later. Leaflet (BSD) if a map is ever embedded.

## Architecture

```
 remote box (native cler)                                   laptop
 ┌──────────────────────────────────────────────────────┐   ┌─────────────────┐
 │ main thread: owns WebServer + Receiver               │   │ index.html      │
 │                                                      │   │ + 4 ES modules  │
 │  Receiver (one flowgraph, stop/reconfigure/run)      │   │  waterfall      │
 │   SourceMux ─> Fanout ─┬─> SpectrumBlock ─> WebSink ─┐ │   │  audio worklet  │
 │                  ├─> Shift ─> Resamp ─> Demod ───┤   │   │  control panel  │
 │                  ├─> decoders ─> JSON adapters ──┤   │   │  decoder tabs   │
 │                  └─> SigMFRecorder               │   │   └─────────────────┘
 │                                                  v   │
 │  WebServer (own thread, outlives graphs) ── ws/http ─┼─ ssh -L ──────────┘
 └──────────────────────────────────────────────────────┘
```

### SourceMux: one source block over every backend compiled in

Users do not type device strings; they pick from a list. `SourceMux` is a
`std::variant` over the source blocks cler already has — native `SourceHackRFBlock`,
`SourcePlutoBlock`, `SourceUHDBlock`, `SourceCaribouliteBlock` (CaribouLite's Soapy
module is not in apt; its native block is the one that works), `SourceSoapySDRBlock`
(exotic and network devices: `driver=remote`, `driver=uhd,addr=…`,
`driver=plutosdr,uri=ip:…`), `SourceSigMFBlock` (playback) and `SimSourceBlock`
(tone + noise, ~40 lines, for CI/Playwright/demos). Each alternative exists only
if its library was found at configure time, the same `#ifdef`s spike uses.

Per kind, two small static/instance additions:
- `enumerate()` → `[{id, label, serial/address}]`: `hackrf_device_list`,
  `iio_create_scan_context` (USB + `ip:`), `uhd::device::find("")`, CaribouLite
  detect, `SoapySDR::Device::enumerate()`, files in `--record-dir`, "Simulator".
- `capabilities()` → the `controls[]` list the UI renders: HackRF LNA/VGA/AMP,
  Pluto gain/bandwidth, UHD gain/antenna, CaribouLite its channels, Soapy from its
  own API, SigMF a read-only RF group + transport, Sim a tone frequency.

`SourceMux` starts empty (`monostate`); `select(kind, id)` closes the old device
and opens the new one; `lost()` reports a dead stream so the app reopens rather
than exits. Exactly one real device plugged in → auto-select at start.

### Switching sources in place

Everything downstream is rate-fixed at construction, so a switch is a stop/
reconfigure/run, the same pause every SDR program has (GQRX, SDR++, OpenWebRX,
GNU Radio's lock/unlock):

```cpp
fg.stop();
mux.select(kind, id);                    // the ~1 s: close + open device
resamp.set_ratio(240e3 / mux.rate());    // MultiStageResampler: recreate msresamp
spec.set_rate(mux.rate());
fg.reset();                              // new: every connected channel -> empty
fg.run();
```

Two small additions to cler: `ChannelBase::reset()` (SPSC read = write, ~5 lines)
and `FlowGraph::reset()` that walks each `BlockRunner`'s output channels
(`cler.hpp:191`) and panics if called while running. Blocks with filter state reset
themselves on reconfigure (the `AnalogDemodBlock::apply_mode` pattern). One
`Receiver`, graph type fixed, no variant-of-receivers, no destroy/construct. The
internal rate is pinned at 240 kHz (scanner's choice) so `AnalogDemodBlock` never
changes; the spectrum runs at the source rate. Switching is refused while recording.

All control (tune, gains, mode, select, record) is applied on the main thread
between `procedure()` calls — atomics/mailbox, the `set_mode` pattern. The server
thread never touches DSP state.

### SpectrumBlock (new, phase 1)

`PlotCSpectrumBlock` does its FFT in `render()` on the GUI thread; there is no
headless spectrum today. `SpectrumBlock`: procedure-side FFT (liquid), window,
averaging, fps cap, emits `SpectrumFrame{gen, center, rate, n, db_min, db_step,
u8 bins[n]}` on a `Channel<SpectrumFrame>`. N=1024, 20 fps default. The ImGui
plots can reuse it later.

### WebSinkBlock + WebServer

`WebSinkBlock` is a normal cler sink inside the Receiver: every call it drains
**all** its inputs (spectrum frames, 48 kHz audio, JSON text) whether or not a
client is connected — it never backpressures the DSP chain (else Fanout stalls and
the SDR ring overflows). It copies into preallocated SPSC rings owned by the
`WebServer` (producer = cler worker, consumer = server thread), no allocation in
`procedure()`.

`WebServer` (IXWebSocket, own thread, ticks at 50 Hz, outlives stop/run cycles): drains the rings, fans out
to sockets with per-socket bounded queues. Drops happen only at the socket edge —
spectrum frames and audio chunks alike, each counted per socket and reported in
`stats`. A slow tab cannot play real-time audio anyway. Ping every 5 s, close on
two misses. Socket list mutated only on the server thread.

HTTP: `/` (client), `/health` (JSON: version, git sha, uptime, source, rate,
overflows, recording, free disk), `/recordings/<name>` (GET, bare filenames only,
resolved inside `--record-dir`, `..` and separators rejected), `Cache-Control:
no-store` on the client files.

### Security (phase 1, not later)

- Bind `127.0.0.1` by default. `--bind` for a LAN.
- WebSocket upgrade rejected unless `Origin` is the server's own `host:port`, and
  without a token the `Host` header must name loopback or the bind address (DNS
  rebinding sends a matching Origin+Host pair for evil.com). Any page on the laptop
  can open `ws://localhost:8080`; the tunnel does not stop that.
- `--token`: required whenever bind is not loopback; client sends it as `?token=`
  (IXWebSocket does not echo `Sec-WebSocket-Protocol`); `/health` and `/recordings`
  require it too, client files stay open.
- Record/play accept bare filenames in `--record-dir` only.
- One controller: first socket (or the one presenting the token) may `set`;
  others are viewers and see `state.role="view"`. Controller leaves → next in line.

### Client

`client/index.html` + `proto.js`, `waterfall.js`, `audio.js`, `panel.js`. Vanilla
ES modules, **no npm dependencies in client/**, no build step. CMake globs
`client/*` into one generated header so the binary is self-contained;
`--client-dir` serves from disk for development.

- Waterfall: ring of raw u8 rows (1024 × 1024 = 1 MB) repainted on zoom/resize,
  2D canvas + `putImageData`, devicePixelRatio aware. Spectrum line above with
  passband and offset marker, dB axis from `db_min/db_step`.
- Interaction mirrors the ImGui scanner: double-click = set tuning offset within
  the span (hardware retunes only when the 240 kHz channel would clip the band
  edge); drag on waterfall = retune hardware centre on release; wheel = client-side
  view zoom only.
- Audio: `AudioContext({sampleRate:48000})` (Safari resamples), an AudioWorklet fed
  Int16 chunks, jitter buffer target 100 ms, refill after underrun, trim > 300 ms;
  persistent "click to listen" overlay (autoplay policy) that also resumes the
  context; flush + refill on `seq` gap, reconnect or `gen` change; buffer depth and
  drop counters shown.
- Control panel generated from `hello.controls`. Reconnect with backoff and full
  re-handshake. `localStorage` holds client prefs only (zoom, colormap, volume,
  tab); tuning is server truth from `hello`/`state`.
- Tests: `proto.js`/`panel.js` are pure → `node --test`, zero deps. One Playwright
  spec (already a devDependency in tools/flowgraph_gui/app) launches the binary with
  `--source sim`, asserts hello/state and that waterfall rows arrive. Headless.

## Protocol v1

All multi-byte binary fields little-endian. Every binary frame starts
`u8 type, u8 ver=1, u32 gen, u32 seq`. `gen` is bumped by the server on every
accepted change (tune, source, rate); the client drops frames whose `gen` is not
the current one and clears the waterfall when center/rate/n change.

- `0x01` spectrum: header, `f64 center_hz, f64 rate_hz, u16 n, f32 db_min,
  f32 db_step, u8[n]`
- `0x02` audio: header, `u8 codec (0 = pcm16 48k mono), i16[960]` (20 ms)

JSON text frames:
- server→client
  - `hello`: `{proto:1, version, sources:[{id,kind,label,available}], source,
    controls:[{id,label,type:"range"|"enum"|"bool",min,max,step|options,unit,ro}],
    state, codecs:["pcm16"], spectrum:{n,fps}, role}` — re-sent whole on source switch
  - `state`: `{gen, source, freq, rate, mode, <control id>: value..., recording,
    switching, role}` — echoed after every accepted change; all tabs converge
  - `stats` (1 Hz): `{audio_dropped, spectrum_dropped, buffered_ms, overflows,
    rec_bytes, free_bytes, pos (file)}`
  - `text`: `{stream:"rds"|"adsb"|..., data}`
  - `error`: `{code, msg}`
- client→server
  - `hello`: `{proto:1, token?, accept:{codecs:[...]}}`
  - `set`: `{<control id>: value, ...}` incl. `freq`, `mode`, `offset`
  - `source`: `{id}`; `rescan`: `{}`
  - `record`: `{on, name?}`; `play`: `{name, pos?, pause?, loop?}`

Codec/fps negotiation lives in `hello` from day one so the WAN profile (phase 5)
adds `pcm16@24k`/`opus`, smaller/slower spectrum, without a flag day.

## Record / playback

- Record: `SigMFRecorderBlock` (ci16_le → 12 MB/s at 3 MS/s; USB SSD, not SD).
  `statvfs` checked at start and every 64 MB; stops cleanly at a 200 MB floor and
  reports. Filename server-generated (`<utc>_<freq>.sigmf-*`), optional client
  prefix. Listing + download through the same server.
- Playback: `SourceSigMFBlock` behind a `ThrottleBlock` (the source has no pacing),
  plus new `seek(pos)`, `pause()`, `loop` and an `ended()` flag (today EOF parks
  on `NotEnoughSamples` forever). RF controls `ro`; a `transport` control group.
  Demod/decoders unchanged — same 240 kHz chain.

## Failure modes the server must handle

- Source lost (USB yanked, stream error): `state.source_lost=true`, auto-reopen
  every 2 s, never exit. Overflow counters in `stats`.
- Disk full: recorder stops itself, `error` + `state.recording=false`.
- Tab closed / half-open through ssh: ping/pong reaps it; per-socket queues bounded.
- Source switch while recording: refused with `error`.
- Two controllers: impossible by construction (role).

## Usage

```bash
cler-websdr                     # no args: UI opens on the Devices list
ssh -L 8080:localhost:8080 box  # from any laptop; open http://localhost:8080
```

Devices list = enumeration of every backend compiled in, plus recordings and
Simulator; one real device → auto-connected. Rescan button. Unplug → "source lost",
auto-reopen. Non-CLI users never type a device string. Flags exist for pinning and
systemd: `--source hackrf[:serial] | pluto[:uri] | uhd[:addr] | cariboulite |
soapy:<kwargs> | sigmf:<name> | sim`, `--freq --rate --gain NAME=V --mode --port
--bind --token --record-dir --client-dir --state-file`.

LAN viewers: `--bind 0.0.0.0 --token s3cret`, they open `http://box:8080/?token=…`;
first token holder controls, the rest view. Internet exposure is out of scope —
nginx + basic auth in front if a client insists.

## Ops

- `--state-file` persists last device/tuning/gains/mode as JSON; reboot comes up
  where it was. Config file only if a client asks; systemd `EnvironmentFile` covers it.
- `--version` prints git sha + build date; `/health` returns the same plus live state.
- `misc/websdr/cler-websdr.service` (Restart=on-failure, dedicated user, udev notes
  for HackRF/Pluto/USRP) ships with phase 1.
- RPi build notes + measured CPU at 2.5 MS/s before phase 1 is called done.

## Phases

1. **Scanner in a tab** — SourceMux over HackRF (native) + Sim, `enumerate()`
   /`capabilities()` for those two, Devices list as first screen, select +
   in-place switch (`Channel::reset`, `FlowGraph::reset`, resampler `set_ratio`),
   SpectrumBlock, WebSinkBlock + WebServer (IXWebSocket), protocol v1 incl.
   `gen`/version/Origin/token/role, client (devices, waterfall, audio worklet,
   generated panel, double-click tune), `/health`, `--version`, systemd unit,
   node + Playwright tests on Sim. Done when: FM station audible in the laptop
   browser through `ssh -L` after picking the HackRF from the list, Playwright
   green on Sim, RPi CPU number recorded.
2. **Record / playback** — recorder start/stop with disk guard, listing, download,
   SigMF source pacing/seek/pause/loop/ended, transport controls, recordings in
   the Devices list.
3. **All backends** — Pluto, UHD, CaribouLite, Soapy alternatives in SourceMux with
   their `enumerate()`/`capabilities()`; verified on whatever is on the desk
   (Pluto available again as of 2026-08-23), others by build + review. Then retire
   `hackrf_spectrum.cpp`, `pluto_spectrum.cpp`, `cariboulite_spectrum.cpp`,
   `fm_receiver.cpp` (one `sdr_spectrum.cpp` on SourceMux replaces the three) and
   point spike's `spike_source.hpp` at SourceMux.
4. **Decoders as tabs** — JSON adapter blocks for RDS, ADS-B, AIS, APRS, modem/FEC
   stats; tables in the browser. Map later.
5. **WAN profile + polish** — `pcm16@24k`, Opus, deflated spectrum rows, fps/N
   negotiation driven by send-queue depth; `--state-file`; gallery entry (screenshot,
   it is not a wasm demo); SoapyRemote note in README.

Each phase = a branch, a worker, critic review, merge.

## Decisions

- Every source cler already has is in scope: HackRF, Pluto, UHD, CaribouLite, Soapy,
  plus SigMF file and Sim — through SourceMux, native blocks first, Soapy for the
  rest. (Alon, 2026-08-23.)
- Users pick devices from a list; no device strings. (Alon, 2026-08-23.)
- Switch in place (stop/reconfigure/`fg.reset()`/run), not rebuild. (Alon, 2026-08-23.)
- Phase 1 on the x86 bench box; RPi measured before phase 1 is called done.
- Tables before maps. IXWebSocket, not uWebSockets.
