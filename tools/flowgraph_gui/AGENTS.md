# cler flowgraph GUI — design vision and rules

Guidance for anyone (human or agent) changing `tools/flowgraph_gui`. Read this
before touching UI or architecture; the repo-root AGENTS.md covers the DSP
framework itself.

## What this tool is

A projection-editor over real C++ files. The `.cpp` file is the only source of
truth: tree-sitter parses it into a model, the GUI renders the model, and every
edit is a validated text splice back into the file. There is no project file
and no export step. Anything the model cannot prove stays read-only with its
reason attached.

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
- **Left panel — Settings**: everything about *this document and session*:
  file identity, run arguments (argv), flowgraph config, block search paths,
  sample-type legend. No canvas duplicates, no statistics without a decision
  the user could take from them.
- **Right rail**: what the *selection* needs — Inspector (selected block),
  Library (palette to place from), Assistant.
- **Bottom drawer**: code, diagnostics, output.
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
- Palette = `desktop_blocks/` of the resolved repo + configured
  `blockLibraries`. Spliced `#include`s are library-root-relative so files
  stay portable; absolute paths live only in per-user settings.
- Windows follows cler's WSL2 stance; do not build native-Windows split modes.

## When in doubt

Prefer deleting a surface over decorating it. The bar for adding UI is a user
decision it enables, not information it displays.
