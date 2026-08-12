# Flowgraph GUI — session log, open questions, next steps

Written 2026-08-12. Everything below is on `main` and pushed unless marked OPEN.

## Where things stand

`cler-fg` can now open a `.cpp` anywhere on disk, edit it, build it, and run it.
The chain that got there, newest last:

- Library paths: a file outside a cler checkout resolves its palette, includes
  and build against the checkout the app was built from, or a configured
  `clerRoot`. `settings.json` (tauri app config dir) holds `clerRoot` and
  `blockLibraries`; redundant entries are pruned rather than refused.
- Build anywhere: `CLER_EDITOR_SOURCE` in the repo's root `CMakeLists.txt`
  declares an executable for a draft outside the tree. The shadow workspace
  mirrors the resolved root and points that hook at the draft. Draft targets are
  namespaced `cler_draft_<stem>` — without that, `~/Desktop/flowgraph.cpp`
  silently built `desktop_examples/flowgraph.cpp`.
- One `DocumentTarget` (`Example` | `Draft`) decides target name, binary path,
  overlay strategy and configure flags. Deriving that answer twice is exactly
  what produced "the built artifact is missing".
- Wiring: connecting gives the destination its own runner; `add_to_graph`
  repairs a block that has none; `Delete` deletes the block, `Remove from graph`
  is menu-only.
- Render loop: the palette records which block types declare `render()`; the
  parser models the loop in any file; `add_render` / `remove_render` maintain
  it and materialize `GuiManager` + loop + include when a document has none.
  Placing a plot from the palette emits declaration and render call together.

## Decisions and why

**Editor generates code; the user never hand-edits.** This is the north star and
it settled several arguments below.

**No `gui_loop()` framework helper.** Two critic passes. The helper would cover
13 of 16 GUI examples while leaving `spike`, `soapysdr_device` and `hackrf_cw`
hand-written — so the editor needs the loop-body parser *anyway*, and the helper
adds a second shape to support. Verified empirically: the statement-list parser
reads all 16 sites today, including spike's 146-line loop (5 renders) and
soapysdr's inline ImGui.

**Not normalising the three holdouts either.** Making "every per-frame
participant is an `X.render()`" a rule costs spike's frame-timing state machine
(resize-settle counter, capture warmup/baseline handshake — no automated
coverage, hardware-only failure modes), needs new API on four blocks plus
`GuiManager::request_close()`, and two of the four new "renderables" would draw
nothing. The payoff is one word of parser behaviour: *reject* vs *ignore*
unknown statements.

**Editor constraints that follow:** insert renders before `gui.end_frame()`,
remove by identifier, never reorder — spike's retile step is order-critical.

**Renderable detection** = declares `render()` **and** is a block.
`spike/control_panel.hpp` and `channelizer_panel.hpp` have `render()` and are
not blocks. Currently the palette flag is `render()`-only because it scans
`desktop_blocks/`; tighten it if example-local scanning ever lands.

## OPEN — the question to resume on

**Should GUI management move into the framework instead of the editor?**
The idea: mark GUI blocks with a trait, and have `make_desktop_flowgraph` (the
desktop policy only) construct and drive a `GuiManager` automatically when any
member is a GUI block. Then *every* graph gets a window with no loop at all —
hand-written and generated alike — and `add_render` becomes unnecessary.

Worth taking seriously; it would subsume the work above. Points to weigh before
building it:

- Rendering must happen on the main thread (`plots.cpp:81`). A flowgraph that
  owns the loop owns the main thread, so `run()` stops being non-blocking, or
  needs a separate `run_gui()` entry point. That is a real API change.
- The 5 apps with custom loops (spike especially) drive the GUI themselves —
  capture, tiling, ImGui panels. An automatic loop must be opt-out, which puts
  us back to supporting two shapes, the objection that killed `gui_loop()`.
- Blocks are compile-time typed with a progress contract; a "GUI trait" is a
  third citizen class. Check whether it can be a plain `static constexpr bool
  is_gui` detected by the same machinery as `may_block`.
- Upside is real: it fixes headless-file-shows-nothing for hand-written code
  too, not just editor-generated code.

## TODO — generated files must announce themselves

Convention to adopt: **anything a script writes lives in a `generated/`
directory and carries a `_gen` suffix**, so no one mistakes it for source or
hand-edits it. Nothing follows this yet; the checked-in generated artifacts are:

| today | becomes |
|---|---|
| `app/src/fixtures/hello_world.json` (and 7 siblings) | `app/src/generated/hello_world_gen.json` |
| `app/tests/palette.json` | `app/tests/generated/palette_gen.json` |

Ripple to fix in the same change: `app/src/fixtures/index.ts` (the only importer
of the models — it also imports the `.cpp` sources with `?raw`, which are NOT
generated and stay put), `tests/topology.test.ts:3` and `tests/ui.ts:7` for the
palette, and `tools/flowgraph_gui/regen-fixtures.sh` which writes both sets.
Fold the rule into `tools/flowgraph_gui/AGENTS.md` once it holds, next to the
existing "fixtures regenerate via regen-fixtures.sh — never hand-edit them".

Not done here because it is a rename touching test imports and the session was
ending; it is mechanical and should be one commit.

## OPEN — smaller

- `app/src/fixtures/*.json` are parsed models of real `.cpp` files, checked in so
  frontend tests and browser demo mode run without a backend. Regenerate with
  `tools/flowgraph_gui/regen-fixtures.sh` (also regenerates
  `app/tests/palette.json`); never hand-edit. Question raised whether they earn
  their keep — they do for browser mode, but the drift risk is real and bit us
  once (stale `spike.json`). The `generated/` + `_gen` rename above is the
  cheap mitigation.
- User reported "I don't see it" for the render loop — unverified whether the
  running app was rebuilt after `f413796`. First step next session: rebuild,
  drag a plot into a fresh document, confirm the loop is written.
- `~/Desktop/flowgraph.cpp` still needs `{"in"}` (brace list) for the plot's
  port; the palette now seeds that for newly placed blocks only.

## How to verify quickly

```
cd tools/flowgraph_gui/cler-graph && cargo test -q     # 192
cd ../app/src-tauri && cargo test -q                   # 112
cd .. && npm test && npx svelte-check                  # 346, 3 pre-existing errors in tests/ui.ts
```

Screenshots of real UI states: `CLER_SHOTS=/tmp/shots npx vitest run tests/<file>`
— any test calling `shot(page, 'name')` writes a PNG.
