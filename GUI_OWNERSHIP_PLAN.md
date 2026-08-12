# GUI ownership moves into the framework

Written 2026-08-12 on branch `gui-in-framework`, revised twice: first after two
adversarial critics (17 findings), then after design review with the user
replaced `render_loop`+extras with a single explicit frame call and the
GUI-block idiom. Successor to the OPEN question in
`tools/flowgraph_gui/HANDOFF.md`. No backward compatibility inside this repo —
every example migrates, the old hand-written loop shape dies here.

## Why

The editor today parses, maintains and *authors* per-frame render loops
(`add_render` / `remove_render`, loop-body statement parser, GuiManager
materialization). That is the editor orchestrating C++ — exactly the smarts it
must not accumulate. Meanwhile every one of the 16 GUI example sites
hand-writes the same loop, and a hand-written graph with a plot but no loop
silently shows nothing.

## The design

**One loop shape for every app, spike included:**

```cpp
flowgraph.run();
while (!gui.should_close()) {
    gui.render(flowgraph);
}
```

`GuiManager::render(fg)` is a member template (desktop_blocks depends on core,
so the direction is dependency-clean; the reverse — a FlowGraph member — is
impossible because the core cannot see GLFW). One call is one frame:
`begin_frame()`, every `is_gui` block's `render()` in runner order,
`end_frame()`, `frame_sleep()`.

**Rule: if it draws or acts every frame, it is an `is_gui` block in the
graph.** Runner order is render order. There are no extras, no hooks, no
callbacks, no hidden loop. Per-frame ordering constraints are expressed by
runner position — spike's capture logic must run after the plot renders, so
its capture block is listed after the plot blocks.

**The GUI-only block idiom**: a block with no channels whose `procedure()`
returns `cler::Error::NotEnoughSamples`. Retryable, so the scheduler backs off
and parks it; the progress contract is respected. Cost: one mostly-parked
worker slot per panel.

### Core (`include/cler.hpp`) — already landed in stage 1

- `for_each_block(f)` — fold over runners, runner order.
- `block_declares_is_gui_v<Block>` — explicit `static constexpr bool is_gui`
  trait next to `may_block`, same SFINAE machinery, cv-ref-proof (a bare
  `Block&` in a generic lambda would otherwise silently detect false for
  every block). Explicit trait, not `render()`-presence detection: `may_block`
  precedent — behavioral flags are declared, not inferred from a method name.

### Desktop (`desktop_blocks/gui/gui_manager.hpp`)

```cpp
template <typename FG>
void GuiManager::render(FG& fg) {
    begin_frame();
    gui::detail::render_blocks(fg);
    end_frame();
    frame_sleep();
}
```

`gui::detail::render_blocks(fg)` is the windowless fold — kept separate so
the ordering logic stays unit-testable without GLFW (CI is headless). A
`static_assert` inside it requires every `is_gui` block to have `render()`.

Already landed in stage 1 and kept: `request_close()`
(`glfwSetWindowShouldClose`; per-frame code ends the app — spike
`--capture-exit`), `frame_sleep()` + `set_frame_sleep_ms(int)` (default 15;
the corpus slept 20/16/10/2 ms — vsync is the real pacer, the sleep only caps
CPU when vsync is off; spike sets 2), `set_visible(bool)` on
`PlotCSpectrumBlock`, `PlotCSpectrogramBlock`, `TriggerBlock` (separate from
`set_active`, which gates `procedure()`'s data work — documented as distinct;
`render()` early-outs when hidden).

Deleted from stage 1's first cut: `render_all`, `render_loop`, the
extras/`post_render()` protocol. The explicit loop makes them unnecessary,
and with them dies the architecture critic's escape-hatch concern.

On fatal graph error the plots freeze (blocks stop producing); the user closes
the window and the diagnosis is on stderr. `gui.render()` does not poll
`fg.is_stopped()` — unattended runs (capture) exit via `request_close()`.

## Migration — all 16 GUI sites in-repo, no compat

| group | files | work |
|---|---|---|
| trivial | hello_world, frequency_shift, cariboulite_spectrum, hackrf_spectrum, pluto_spectrum, plots, polyphase_channelizer, adsb_receiver, mass_spring_damper | loop body becomes `gui.render(flowgraph);` |
| multi-site | uhd_device (4 sites, sequential) | same replacement per site |
| per-frame stats | hackrf_cw | the per-5s TX underrun report becomes a tiny `is_gui` stats block, listed after the spectrum |
| inline panel | soapysdr_device | the inline ImGui controls become an `SdrControlPanelBlock` (`is_gui`), listed first |
| the hard one | spike | see below |

Render-order flips vs today's hand loops (`mass_spring_damper`,
`hackrf_spectrum`, `pluto_spectrum` render in non-runner order today):
independent ImGui windows, first-frame z-order only. Accepted. Where it
matters, reorder the runners to match.

Runner order also seeds scheduler topo tie-breaking (PinnedIslands islands).
Irrelevant for these desktop apps; documented in AGENTS.md so a tuned graph
knows to check.

### Spike

The 146-line loop splits by frame position into two new blocks; runner order
pins the frame layout that its in-source comments demand:

```cpp
cler::BlockRunner(&panel),      // ControlPanelBlock: UI, set_active/set_visible
                                // sync, retile state machine
cler::BlockRunner(&source, &fanout.in),
cler::BlockRunner(&fanout, ...),
cler::BlockRunner(&power, &trigger.in),
cler::BlockRunner(&trigger),
cler::BlockRunner(&spectrum),
cler::BlockRunner(&spectrogram),
cler::BlockRunner(&channelizer),
cler::BlockRunner(&capture)     // CaptureBlock: warmup/baseline machine,
                                // .dat+PNG snapshot, --capture-exit
```

- `ControlPanelBlock` (from today's `ControlPanel`): renders the panel window,
  syncs `set_active`/`set_visible` from its show_* flags, retiles at the end
  of its `render()` — after the panel window is drawn, before the plots
  render, same frame position as today.
- `CaptureBlock`: owns the capture-mode fields of `SpikeArgs` (the panel never
  copied them), pointers to trigger/plots/panel and the `GuiManager` (for
  `request_screenshot` and `request_close`), exposes `timed_out()` so
  `run_app` keeps computing its exit code and the capture-mode `panel.save`
  skip after the loop. Renders after the plots: `.dat` export matches the
  displayed frame, PNG (written in `end_frame`) matches the same frame.
- The loop in `run_app` becomes the canonical two-liner.

Verification: `--capture` runs on real hardware write PNG+`.dat`; compare
before/after, including first-capture behaviour. Manual gate, hardware-only.

## Editor changes (tools/flowgraph_gui)

- **Delete**: `add_render` / `remove_render` commands, the loop-body statement
  parser as a write surface, GuiManager+loop materialization in its current
  form.
- **The canonical loop is constant text.** `while (!gui.should_close()) {
  gui.render(flowgraph); }` never changes when blocks are added or removed —
  the editor splices it once (with the `GuiManager` declaration) when the
  first `is_gui` block lands, anchored to the modeled `flowgraph.run(...)`
  statement, and never edits its body. Zero loop maintenance forever.
- **Panels are blocks.** They appear on the canvas, in the palette (from
  headers or the open file), and their render position is edited by
  reordering runners — a command the editor already has.
- **Keep a legacy-loop detector.** Out-of-tree files keep old-style hand
  loops forever; without detection, the materialization gesture would splice
  a second GuiManager+loop into them. A cheap query (a `GuiManager`
  declaration or `begin_frame` call outside the canonical form) marks the
  document `legacy_gui`: materialization disabled with the reason shown,
  blocks/params/topology stay editable, the old loop renders as opaque code.
- Palette: the render flag becomes the `is_gui` flag; fixtures regenerate.

## Stages

1. **Core + desktop machinery.** Landed (commit 113555f), then reworked:
   delete `render_all`/`render_loop`/extras, add `GuiManager::render(fg)` +
   `gui::detail::render_blocks(fg)`, retarget the unit tests at
   `render_blocks` ordering and the trait.
2. **Migrate the 15 non-spike sites** (trivial + uhd + hackrf_cw stats block
   + soapysdr panel block). Build all, spot-run locally.
3. **Spike restructure** (`ControlPanelBlock` + `CaptureBlock`). Manual
   capture-mode verification on hardware, including first-capture.
4. **Editor.** Delete write-side loop machinery, add legacy detector +
   constant-loop materialization + `is_gui` palette flag, regen fixtures,
   full test suites.
5. **Docs.** Root AGENTS.md (§4 gains `is_gui` + the GUI-only block idiom,
   §6 gains the canonical loop, runner-order note), `README.md:76-78` and
   `docs/index.html:101-102` (both embed the dead loop shape verbatim), gui
   AGENTS.md, HANDOFF.md OPEN question closed, this file marked done.

Each stage compiles, passes tests, commits and pushes before the next starts.

**Status 2026-08-12: stages 1-5 landed** (113555f, 8bf8050, a363bab, 14be366,
docs commit). Crate 196 / tauri 112 / frontend 346 / framework 20 tests green.
Outstanding: spike capture-mode verification on real hardware (stage 3 gate).

## Risks

- Spike is the only consumer of half these mechanisms and has hardware-only
  failure modes; stage 3 is gated on manual verification and lands separately.
- The legacy detector is the one piece of old-shape knowledge that survives;
  its scope is fixed (detect, mark, refuse) and must never grow back into
  loop maintenance.
- GUI-only blocks occupy scheduler slots (parked). If a graph ever has many
  panels, revisit; one or two per app is noise.
- ImGui first-frame z-order changes in three examples (cosmetic, listed).
