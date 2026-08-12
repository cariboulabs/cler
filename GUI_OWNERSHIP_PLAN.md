# GUI ownership moves into the framework

Written 2026-08-12 on branch `gui-in-framework`. Successor to the OPEN question
in `tools/flowgraph_gui/HANDOFF.md`. Decision: the render loop is framework
machinery, not user code and not editor-generated code. No backward
compatibility inside this repo — every example migrates, the old hand-written
loop shape dies here.

Reviewed adversarially by two independent critics (one correctness refuter
verifying every claim against the tree, one architecture refuter grounded in
AGENTS.md / HANDOFF.md / the editor plan). This version incorporates all 17
findings; the draft's refuted claims are corrected inline.

## Why

The editor today parses, maintains and *authors* per-frame render loops
(`add_render` / `remove_render`, loop-body statement parser, GuiManager
materialization). That is the editor orchestrating C++ — exactly the smarts it
must not accumulate. Meanwhile every one of the 16 GUI example sites
hand-writes the same loop, and a hand-written graph with a plot but no loop
silently shows nothing.

One framework entry point deletes both problems: hand-written and generated
files get a window from the same machinery, and the editor's render-loop
knowledge shrinks to one grammar production plus a legacy detector.

## The API

### Core (`include/cler.hpp`) — one GUI-agnostic addition

```cpp
template <typename F> void for_each_block(F&& f);   // f(block_ref), runner order
```

A fold over `_runners`. Nothing else: the draft's `stopped()` was refuted —
`is_stopped()` already exists (`cler.hpp:483`) with the right acquire
semantics, and fatal errors set `_stop_flag` on both execution paths
(per-block threads and pool/islands workers), so loop exit on graph death
already has its accessor.

The GUI trait sits next to `may_block`, same machinery, same explicitness:

```cpp
struct PlotCSpectrumBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;   // framework renders this every frame
    void render();
    ...
};
```

Detected via `block_declares_is_gui_v<Block>` (same SFINAE pattern as
`block_declares_may_block_v`, `cler.hpp:175-180`). **Explicit trait, not
`render()`-presence detection.** The draft chose detection to match the
palette's zero-registration discovery; refuted: the palette's rule is
advisory (lists a block in a UI), this one is behavioral (the framework calls
a method every frame on the GUI thread, concurrent with `procedure()` on a
worker). The codebase's only comparable flag, `may_block`, is deliberately
explicit for exactly this reason. The trait is also the opt-out for a block
whose `render()` the user drives manually, and it is as greppable for the
palette as `render()` is. A `static_assert` inside the render fold requires
that every `is_gui` block actually has `render()`.

Trait detection must be reference-proof: the fold's lambda sees `Block&`, and
a naive `Block::is_gui` partial specialization is silently false for
reference types — every block skips rendering and it compiles clean. The
trait strips cv-ref (`std::remove_cvref_t` equivalent in C++17) and a unit
test covers the reference case explicitly.

### Desktop (`desktop_blocks/gui/gui_manager.hpp`)

```cpp
namespace cler::gui {

// One frame's worth of rendering, no window machinery -- unit-testable
// without GLFW against mock blocks:
//   extras with render()      -- before the blocks, in argument order
//   blocks with is_gui        -- runner order
//   extras with post_render() -- after the blocks, in argument order
template <typename FG, typename... Extras>
void render_all(FG& fg, Extras*... extras);

// Blocks until the window closes or the flowgraph stops. Does NOT run/stop
// the flowgraph -- caller does; real code lives between run() and the loop
// (spike's panel.apply_all()).
template <typename FG, typename... Extras>
void render_loop(GuiManager& gui, FG& fg, Extras*... extras);

}
```

`render_loop` is the thin shell:

```cpp
while (!gui.should_close() && !fg.is_stopped()) {
    gui.begin_frame();
    render_all(fg, extras...);
    gui.end_frame();          // screenshots are written in here, as today
    gui.frame_sleep();
}
```

An extra may implement `render()`, `post_render()`, or both; each hook is
detected independently (same cv-ref-proof machinery). **Post-render hooks are
load-bearing, not future-proofing**: spike's capture/snapshot machinery is
documented in-source as required to run *after* the plot renders and *before*
`end_frame()` (`spike.cpp:255` "Consumed after render() (exported data
matches this frame)"; `plot_cspectrum.hpp:46-50` — `export_spectrum` returns
the currently *displayed* spectrum and fails before the first render). The
draft put capture in a before-blocks extra; refuted — that skews `.dat`
against `.png` by one frame and can fail the first capture outright.

`GuiManager` gains:

- `request_close()` — `glfwSetWindowShouldClose`; per-frame code ends the app
  programmatically (spike `--capture-exit`).
- `frame_sleep()` + `set_frame_sleep_ms(int)`, default 15. The draft claimed
  a uniform "2 ms convention"; refuted — the corpus sleeps 20/16/10/2 ms
  across sites. Vsync is the real pacer; the sleep only caps CPU when vsync
  is off. One default plus a setter covers spike's 2 ms without a new
  parameter on `render_loop`.

### Plot visibility: `set_visible`, not `set_active`

Auto-rendering removes the call site's `if (panel.show_x) x.render()` guard,
so hiding moves into the block: `set_visible(bool)`; `render()` early-outs
when hidden. **Separate switch from `set_active`** — refuted draft claim: the
headers document active as gating `procedure()`'s data work ("drain without
copying", `plot_cspectrum.cpp:116`) and explicitly distinct from display
freezing; merging them changes documented semantics. Also `TriggerBlock` has
no `set_active` at all, and spike gates its scope window — so `set_visible`
is added to all four spike-rendered types: `PlotCSpectrumBlock`,
`PlotCSpectrogramBlock`, `TriggerBlock`, `ChannelizerPanelBlock`. Default
visible; only panels ever call it.

### What a GUI file looks like after

```cpp
cler::GuiManager gui(1500, 800, "Plots Example");
// ... block declarations ...
auto flowgraph = cler::make_desktop_flowgraph(/* runners */);
flowgraph.run();
cler::gui::render_loop(gui, flowgraph);
```

No `stop()` in the canonical editor-generated shape — `~FlowGraph()` stops
(`cler.hpp:269`), and the editor must not generate cargo-cult lines. Files
with post-loop code that reads block state (overflow prints, settings save)
call `stop()` explicitly first, as several examples do.

On fatal graph error the loop exits and the window closes; the diagnosis
lives on stderr. Keeping a dead window open showing the last frame was
considered and rejected: it hangs every unattended run (capture, e2e).

## Decisions and why (surviving the critics)

**Caller constructs `GuiManager`; `render_loop` does not.** A `WindowConfig`
mirroring GuiManager's ctor would be a second way to say the same thing.
Declaring `gui` is one line the editor already knows how to splice, and spike
needs the object before the loop (`io.IniFilename`, `request_screenshot`).

**`render_loop` does not own `run()`/`stop()`.** Real code lives between
`run()` and the first frame and after the loop. Owning them would force a
callback API for those spots.

**Blocks render in runner order.** The one order the file already fixes and
the editor already controls. Order flips vs today's hand loops:
`mass_spring_damper` (plant/plot/controller → controller/plant/plot),
`hackrf_spectrum` and `pluto_spectrum` (timeplot/spectrogram →
spectrogram/timeplot). All are independent ImGui windows; first-frame z-order
only. Accepted, and listed honestly this time.

## Migration — all 16 GUI sites in-repo, no compat

| group | files | work |
|---|---|---|
| trivial | hello_world, frequency_shift, cariboulite_spectrum, hackrf_spectrum, pluto_spectrum, plots, polyphase_channelizer, adsb_receiver, mass_spring_damper | replace loop with `render_loop(gui, fg)` |
| multi-site | uhd_device (4 sites, sequential) | same replacement per site |
| loop has non-render logic | hackrf_cw | the per-5s TX underrun report (`hackrf_cw.cpp:154-162`) moves to a tiny `UnderrunReporter` extra with `post_render()`; mechanical replacement would silently delete it (refuted "trivial" bucketing) |
| inline panel | soapysdr_device | hoist the inline ImGui controls into an `SdrControlPanel` struct with `render()`; pass as extra |
| the hard one | spike | see below |

### Spike

The 146-line loop splits by frame position, not by "who holds the pointers"
(the draft's pointer-ownership framing was refuted — frame position was
always the constraint, and the in-source comments pin it):

- **`ControlPanel::render()`** (before blocks, as extra #1): panel UI, the
  per-frame `set_active`/`set_visible` sync, and the retile state machine
  (resize-settle counter, equal-height rows). Retile runs at the end of
  `render()` — after the panel window is drawn, before the plots render;
  same frame position as today. Panel gains nothing else.
- **`CaptureDriver::post_render()`** (after blocks, extra #2): the capture
  warmup/baseline machine, the snapshot `.dat`+PNG write, `--capture-exit`
  via `gui.request_close()`. A separate struct, not more panel: it owns the
  capture-mode fields of `SpikeArgs` (which `ControlPanel` never copied —
  refuted "already holds what it needs"), pointers to trigger/plots/panel/
  gui, and exposes `timed_out()` so `run_app` keeps computing its exit code
  (`capture_timed_out ? 2 : 0`) and the capture-mode `panel.save` skip after
  the loop. This also answers the cohesion critique: the panel stays a
  panel; orchestration lives in its own ~80-line object.

Call site: `run()`, `apply_all()`, `render_loop(gui, fg, &panel, &capture)`,
`stop()`, save/report as today.

Verification: `--capture` runs on real hardware write PNG+`.dat`; compare
before/after, including first-capture behaviour (the export-before-render
failure mode the draft would have shipped). Manual gate, hardware-only.

## Editor changes (tools/flowgraph_gui)

- **Delete**: `add_render` / `remove_render` commands, the loop-body
  statement parser as a *write* surface, GuiManager+loop materialization in
  its current form.
- **Keep a legacy-loop detector.** Refuted: "the loop parser dies" was only
  true inside this repo. The editor opens arbitrary out-of-tree files
  (`~/Desktop/flowgraph.cpp`), which keep old-style hand loops forever. With
  no detector, such a file looks like "a document without a render_loop" and
  the materialization gesture would splice a *second* GuiManager and loop
  into it. The detector is a cheap query — a `GuiManager` declaration or a
  `begin_frame` call outside the `render_loop` grammar — and its only job is
  to mark the document `legacy_gui`: GUI materialization gestures disabled
  with the reason shown, blocks/params/topology stay editable, the old loop
  renders as opaque code. No maintenance of loop contents, ever.
- **New grammar productions**: `cler::GuiManager <id>(w, h, title);`
  declaration and `cler::gui::render_loop(<gui>, <fg>, <extras...>);`
  statement. Extras are opaque read-only elements.
- **Materialization gesture**: placing the first `is_gui` block into a
  non-legacy document without a `render_loop` splices the gui declaration
  and the `render_loop` call. The insertion anchor is the modeled
  `flowgraph.run(...)` statement — an element the model already carries, not
  free statement-list reasoning; if `run()` isn't modeled (opaque site), the
  gesture is refused with the reason, same rule as everywhere else. Removing
  the last `is_gui` block removes both. (Honest accounting, per the critics:
  this is still one insertion decision — but anchored to a modeled element,
  which is the planner's normal splice mode, not today's arbitrary
  loop-body positioning.)
- Palette: the render flag becomes the `is_gui` flag (greps the same way);
  fixtures regenerate (`regen-fixtures.sh`).

## Stages

1. **Core + desktop machinery.** `for_each_block`, `is_gui` trait
   (cv-ref-proof + static_assert on `render()`), `render_all` +
   `render_loop`, `request_close`, `frame_sleep` setter, `set_visible` on
   the four block types. Unit tests: trait incl. reference case,
   `for_each_block`, `render_all` ordering (pre-extras / blocks / post-extras)
   against mock blocks — no GLFW needed since `render_all` is windowless.
   `render_loop` itself compile-tested (CI is headless).
2. **Migrate the 15 non-spike sites** (trivial + uhd + hackrf_cw reporter +
   soapysdr panel hoist). Build all, spot-run locally.
3. **Spike restructure** (`ControlPanel::render` + `CaptureDriver`).
   Manual capture-mode verification on hardware, including first-capture.
4. **Editor.** Delete write-side loop machinery, add legacy detector + two
   grammar productions + anchored materialization gesture, regen fixtures,
   full test suites.
5. **Docs.** Root AGENTS.md (§4 gains `is_gui` + render contract, §6 gains
   `render_loop`), `README.md:76-78` and `docs/index.html:101-102` (both
   embed the dead loop shape verbatim — found by the correctness critic),
   gui AGENTS.md, HANDOFF.md OPEN question closed, this file marked done.

Each stage compiles, passes tests, commits and pushes before the next starts.

## Risks

- Spike is the only consumer of half these mechanisms and has hardware-only
  failure modes; stage 3 is gated on manual verification and lands separately.
- The legacy detector is the one piece of old-shape knowledge that survives;
  its scope is fixed (detect, mark, refuse) and must never grow back into
  loop maintenance.
- `post_render()` is the sanctioned home for per-frame logic; the temptation
  to add lambdas or more hook points is the escape hatch the architecture
  critic warned about. Two hooks, method-named, on objects — nothing else.
- ImGui first-frame z-order changes in three examples (cosmetic, listed).
