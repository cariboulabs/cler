# cler flowgraph GUI — design vision and rules

Guidance for anyone (human or agent) changing `tools/flowgraph_gui`. Read this
before touching UI or architecture; the repo-root AGENTS.md covers the DSP
framework itself.

## What this tool is

A projection-editor over real C++ files. The `.cpp` file is the only source of
truth: tree-sitter parses it into a model, the GUI renders the model, and every
graph gesture is a validated text splice back into the file. There is no
project file and no export step. Anything the model cannot prove is marked in
the code with its reason attached — a mark the canvas obeys and the text
editor only shows.

The code drawer is a real editor (CodeMirror 6), so the same file can be
changed by hand. Typed text settles into one `edit_source` commit: it becomes
a single tight splice, one undo step, the same history as any graph gesture.
Text that tree-sitter cannot parse never enters the session — it stays in the
buffer and in the working copy, and the drawer says where the parser lost the
thread and what it expected. While it does not parse the buffer is ahead of a
model that cannot follow it, so **every gesture that would move the model is
refused with that reason** — save, build, undo, canvas edits alike — and the
way out is to fix the syntax or press *discard edit*. Every gesture also
flushes pending text first, so the two never race for a revision. A draft in
the working copy is adopted on open only if it still parses.

## Architecture map

- `cler-graph/` (Rust lib + CLI): parse → `FileModel` (sites, blocks, runners,
  edges, config), commands → splices (`apply/planner.rs`), palette extraction
  from block headers. Pure; no app state.
- `app/src-tauri/`: document sessions, unified undo (`ActionQueue<Action>`),
  drafts + shadow builds, artifact provenance, app settings
  (`settings.json`: `clerRoot`, `blockLibraries`), jobs (check/build/run).
- `app/src/`: Svelte 5 (runes only — `$state`, `$derived`, `$effect`,
  `$props`; never Svelte 4 syntax). `FieldList.svelte` is the single field
  editing machinery (drafts, IME, focus restore, errors) — reuse it, never
  duplicate it.

**Correlation rule**: every UI surface mirrors one model concept. If a widget
has no backing model concept, question the widget. If a model concept shows up
in two places under two names, one of them is wrong.

## Layout contract

- **Top bar**: identity (cler mark), File menu, document path (readonly,
  full real path, monospace), problems chip, task actions (check/build/run),
  history, view controls. Nothing else.
- **Left panel — AI Agent | Settings**: two tabs. Settings holds everything
  about *this document and session*: file identity, run arguments (argv),
  flowgraph config, block search paths, sample-type legend. No canvas
  duplicates, no statistics without a decision the user could take from them.
  AI Agent is the chat over this flowgraph (Ctrl+J).
- **Right rail**: what the *selection* needs — Inspector (selected block),
  Library (palette to place from).
- **Bottom drawer**: code (editable), diagnostics, output.
- One home per concept. The palette browser is "Library" (right); the search
  paths that feed it are "Block paths" (left). Never reuse a name.

## UX rules

1. **Disabled means explained.** Every disabled control carries its reason —
   visible text next to it when space allows, tooltip at minimum. "Can't
   click" without "why" is a bug.
2. **Native widgets, established patterns.** Enumerations are `<select>`;
   free values are text inputs with the default as placeholder; menus are
   menus. Do not invent widget kinds.
3. **Show effective values.** An unset config field shows the default it will
   get, not an empty void.
4. **Empty sections disappear.** A section with nothing to say (0 read-only
   notes, no config) is hidden, not rendered with a zero.
5. **Code-flavoured values** (paths, C++ expressions, block types) render in
   monospace. Prose renders in the UI font.
6. **Destructive actions confirm; reversible actions don't ask.**
7. **Every interactive element has a `data-testid`** and behaviour lands with
   a test. Fixtures regenerate via `regen-fixtures.sh` — never hand-edit them.
8. **Panels hold values, not loose push-buttons.** A panel section shows
   fields, lists, and read-only values; actions on that section live in a
   small `⋯` menu on the section header (see Block paths). Inline icon
   affordances on list rows (a `✕` to remove) are fine; free-standing
   buttons in the panel body are not.

## Building a document

A file inside `desktop_examples/` builds through its own CMake target. Any
other file — anywhere on disk — builds as a *draft target*: the shadow
workspace mirrors the resolved cler root, and the repo's
`CLER_EDITOR_SOURCE` hook creates `cler_draft_<stem>` for it. Draft targets
are namespaced precisely so a file named like a repo example cannot silently
build that example instead. Both paths share exact block linking, artifact
provenance, and Run gating; nothing about the shadow build is special-cased
per document.

## Product constraints

- No wizard for defining new block *types* — blocks are authored in C++ and
  discovered by the palette (repo AGENTS.md rule).
- The editor never authors or edits render-loop bodies. The framework owns
  rendering (`gui.render(flowgraph)`, repo AGENTS.md "GUI blocks");
  `materialize_gui` splices the GuiManager declaration and the constant
  two-line loop once, anchored after `run()`, and never touches them again.
  Hand-written loops (`begin_frame`/`end_frame`) parse as `gui.legacy` and are
  refused, never modified. The palette's `is_gui` flag comes from the trait.
- Palette = `desktop_blocks/` of the resolved repo + configured
  `blockLibraries`. Spliced `#include`s are library-root-relative so files
  stay portable; absolute paths live only in per-user settings.
- Windows follows cler's WSL2 stance; do not build native-Windows split modes.

## When in doubt

Prefer deleting a surface over decorating it. The bar for adding UI is a user
decision it enables, not information it displays.

## Browser builds

- `cler-web/` — the document session (open/apply/edit/undo/palette) compiled
  to `wasm32-wasip1` behind a JSON `cler_invoke` ABI; same command names and
  reply shapes as the Tauri backend, minus filesystem, build, run and agent
  (those refuse with "needs the desktop app"). Files and `desktop_blocks/*.hpp`
  are bundled by `app/src/fixtures/files.ts`; `app/src/lib/wasmbridge.ts`
  installs it as `__TAURI_INTERNALS__` so the app sees a desktop shell.
- `docs/try/` — the static site build of that: `npm run build:web` in `app/`
  (needs `rustup target add wasm32-wasip1` and `WASI_SDK=` a wasi-sdk
  checkout for tree-sitter's C). Regenerate after UI, fixture, or block-header
  changes and commit the output. `tests/wasm_session.test.ts` covers the wasm
  end to end and skips when it is not built.
- Run in the browser — `web-run/build.sh` (needs `EMSDK=` an emsdk checkout with
  **3.1.24** activated, the version emception ships) builds liquid-dsp + GUI/plot
  blocks into `libcler_web.a`, the examples in `src/fixtures/files.ts::RUNNABLE`
  into `app/public/run/`, and the *payload* the in-browser compiler needs into
  `app/public/payload/`: the two archives plus `headers.json` (`include/**`, the
  imgui/implot headers, `liquid.h`, `shell.html` — `desktop_blocks/**` already
  ships inside the app bundle). Rerun it after touching those headers or flags;
  `CXXFLAGS`/`LDFLAGS` in `src/lib/emception.ts` must stay in step with the script.
- Build in the browser — `src/lib/emception.ts` runs em++ under
  [emception](https://github.com/jprendes/emception) (clang + lld + the emscripten
  sysroot, in wasm) over a virtual repo rooted at `/working`, so every path on the
  command line — and in the diagnostics coming back — is the app's own
  repo-relative path. The toolchain itself is fetched from `TOOLCHAIN_BASE`
  (emception's GitHub Pages); self-hosting is a base-URL swap. Compile is `-O2`,
  link is `-O1` (`-O2` links take minutes).
- `public/cler-sw.js` is the one service worker: it adds COOP/COEP to every
  response (the coi-serviceworker trick, MIT), mirrors the cross-origin toolchain
  under `emception/*` so its worker bundle can resolve its own assets by relative
  URL, and serves `built/*` out of Cache Storage. `main.ts` registers it and
  reloads once on the first visit; if registration fails (private windows) the
  editor still mounts and Check/Build/Run refuse with that reason. The mirrored
  bundle runs as same-origin code, so its four entry files are pinned by sha256 in
  `TOOLCHAIN_PINS` — bump `TOOLCHAIN_BASE` and you must recompute them, and the
  toolchain cache is keyed by origin so the old one is dropped.
- `wasmbridge.ts` answers `find_target`/`check_document`/`build_target`/`run_target`:
  a bundled example still equal to the bundle runs straight from `run/<name>.html`;
  anything else compiles and links into `built/<sha of source>/app.html` and Run
  pops that. Both run windows are cross-origin isolated, so pthreads work.
- Waiting, in the browser — a build takes 15-60 s, so `src/lib/BuildProgress.svelte` takes over the
  top band of the bottom drawer for the duration — the drawer reserves `STRIP_H` pixels (its `inset`
  prop) so the canvas is never covered, and when the drawer is collapsed the same surface shrinks to
  a pill at bottom centre that opens it (browser only: `inTauri()` is true under the wasm shell
  too, so the gate is the `VITE_CLER_WASM` build flag). It shows nothing it did not observe:
  `emception.ts` and `wasmbridge.ts` call `phase()` from `src/lib/progress.ts` at the real
  transitions — toolchain download (cumulative service-worker bytes over `TOOLCHAIN_BYTES`),
  boot, staging the payload, compile (with the file), link, wasm-opt *only* when em++ names it,
  store, launch — and the panel maps the event to one phrase in the UI font
  with its target (file, byte count) in monospace beside it, over a 3 px full-bleed track:
  determinate where there are numbers (bytes, or per-phase durations banked in `localStorage` by the
  last run), a shimmer where there are none. Under the track one muted line — a cler fact, or the
  25 MB first-build note while the toolchain downloads. A finished job flashes the track green and
  folds away; a failed one keeps a red track with the first error and a jump to Diagnostics, and
  Output keeps the raw em++ log. `tests/progress.test.ts` covers the mapping.
- `web-run/smoke.mjs` is the end-to-end check: `node ../web-run/smoke.mjs` from
  `app/` after `npm run build:web` serves `docs/` with a header-less
  `python3 -m http.server` (as Pages does) and drives edit → check → build → run
  → screenshot in headless Chromium.
