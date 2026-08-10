# Cler Flowgraph GUI — Architecture

A GNU Radio Companion-style desktop editor for cler flowgraphs. Replaces
`tools/mermaid` (vscode extension + mermaid preview).

Reviewed adversarially by three independent critics (two architecture/parsing
refuters grounded in the full `desktop_examples/` + `desktop_blocks/` corpus,
one external second opinion). This version incorporates their findings; the
draft's refuted claims are corrected inline and marked with their evidence.

## The one rule, and its three exceptions

**The `.cpp` file is the document.** No project file, no YAML, no JSON on
disk. The GUI parses C++ and patches C++. A frontend that keeps its own
persistent graph document has reinvented the middle format.

Three things genuinely cannot live in the `.cpp`, and are named as the only
exceptions rather than smuggled in:

1. **Node positions** — app-local cache keyed by (file path, graph site,
   block name), deterministic elkjs auto-layout on miss. Explicitly
   per-machine and not shareable in v1; if layout sharing is ever needed, the
   candidate is an opt-in comment line in the file, decided then, not now.
2. **In-flight text edits** — a param field holds transient local state and
   commits one command on blur/Enter; sliders commit on pointer-up. The rule
   is "no *persistent* document state in the frontend", not "no text box".
3. **Half-wired blocks** — a block dropped from the palette writes its
   *declaration* immediately (unused variable compiles; repo sets
   `-Wno-unused-variable`) but enters the runner list only once its wiring
   satisfies its arity. Unwired nodes render as such. The build is never
   broken by an in-progress gesture.

## Stack and ownership

- **`cler-graph`** — headless Rust crate, tree-sitter-cpp. Owns the
  **DocumentSession**: source bytes, tree, monotonic revision, undo/redo
  snapshots, command validation and application. Ships a CLI (`parse`,
  `lint`, `apply`, `palette`) with a versioned JSON output — the CLI is a
  public scripting interface; the IPC JSON is not (crate and frontend ship
  in one binary, they cannot skew).
- **Tauri v2 shell** — OS effects only: file dialogs, watcher, atomic saves,
  process spawning, layout cache. Links the crate directly. It never touches
  document bytes except through the session.
- **Svelte Flow frontend** (xyflow) — renders the latest revision-tagged
  projection. Transient UI state only (selection, zoom, positions, wire
  preview, uncommitted field text). Chosen over React Flow after checking
  the full M1-M3 canvas requirement list (custom nodes/handles, parallel
  edges, cycles, external positions, validation callbacks, palette DnD,
  reconnect) — Svelte Flow 1.x covers all of it on the same xyflow core,
  and its fine-grained reactivity fits the pushed-projection pattern
  without React's memoization ceremony.

Undo/versioning live in the crate, not the shell, so CLI and GUI edits share
one transaction path and the tree can never be stale relative to the writer.

## Edit loop

Every gesture is a command: `set_param`, `set_display_name`, `add_block`,
`remove_from_graph`, `delete_block`, `connect`, `disconnect`, `set_config`,
`set_arity`.

1. Frontend sends command + the revision it was computed against.
2. Session rejects on revision mismatch (re-pushes current state), else
   patches byte ranges, incrementally re-parses, bumps revision.
3. Fresh projection pushed; frontend re-renders.

- One source-mutating command in flight per file; later drafts coalesce.
- **A gesture is one atomic patch**: connecting a fourth input to
  `AddBlock<float, 3>` rewrites the template arg *and* splices the runner
  argument in a single multi-range patch with a single undo snapshot. Two
  writes for one gesture is how a crash leaves a wire into an unconstructed
  channel.
- **Undo** restores a byte snapshot via the session. The stack is **cleared
  when an external reload is accepted** — otherwise Ctrl-Z after a reload
  silently destroys code written in another editor.
- **External edits**: watcher event → read file → compare bytes against what
  the session last wrote. Identical = self-write, ignore (no timing window).
  Different = reload banner; before *any* write the session re-checks the
  on-disk hash against its base revision and aborts on mismatch.
- M0 benchmarks the committed-edit round-trip: p95 ack < 50 ms on the
  largest example (`spike.cpp`, 51 KB); structural deltas only if that fails.

## Patch granularity: splice, never regenerate

The draft's "regenerate `make_desktop_flowgraph(...)` wholesale" is refuted:

- **Runner order is semantic.** It seeds topo-sort tie-breaking
  (`cler_topology.hpp`), which fixes where PinnedIslands cuts land, which
  fixes throughput on a tuned 2-core graph. Regeneration reorders silently.
- It erases `BlockRunnerMayBlock` (a per-runner override, not derivable from
  the block type), deliberate formatting/grouping, and comments.
- It emits non-compiling code for real forms: `gain.in()` (method-call port,
  `selectable_blocks.cpp`), `&*source` (`std::optional`,
  `adsb_receiver.cpp`), named runner variables passed as bare identifiers
  (`demod_iq_recording.cpp` — zero `BlockRunner` expressions inside the call).

Therefore: all edits are **argument-level splices** on tree-sitter nodes —
insert/remove one runner argument, append/remove one channel argument,
rewrite one ctor/template argument. Untouched runners survive as their
original byte slices. An explicit opt-in **Normalize** action (with diff
preview) is the only wholesale rewrite that exists.

## Editability envelope

Editing is an earned per-site capability, not a default. Parsing a file
yields a **list of graph sites**, each keyed by enclosing function + call
byte offset, with symbol resolution scoped to that function
(`uhd_device.cpp` has four sites reusing local names `fanout`/`spectrum`;
a file-level name map merges them into garbage). The UI offers a site
selector.

Per site, per element, capability flags:

- **Editable**: every expression involved matches the documented grammar —
  `Type var(args);` declarations, `cler::BlockRunner(&blk, &dst.port...)`
  runners, direct `config.field = value;` assignments.
- **Read-only**: anything else — named-runner form, `optional`/`emplace`,
  method-call ports, alias-typed declarations not resolved by the file-local
  alias table, config built through factories (`flowgraph_config::*`) or
  manual islands. Read-only elements render fully, pin their edges, and
  refuse edits with the reason shown.

Files with no flowgraph call (streamlined mode) or parse failures open
read-only. Rendering never requires editability; the 68% of examples that
are fully editable-grammar is the target for edits, the rest must still
*display* correctly.

## Port model: ports belong to instances, not types

The draft's palette model ("`Channel<T>` members, including arrays") is
refuted by the corpus: **no multi-port block in the repo uses a Channel
array.** They all use `cler::Channel<T>* in` + placement-new over private
`_in_storage[N]`, and the real count comes from one of three authorities:

| authority | example |
|---|---|
| template arg | `AddBlock<T, NumInputs>`, `PolyphaseChannelizerBlock<M, L>` |
| ctor arg | `FanoutBlock(num_outputs)`, `SinkUHDBlock(num_channels)` |
| ctor label-vector size | all three plot blocks (`signal_labels.size()`) |

A wrong wire against the plot blocks **compiles clean and reads
uninitialized memory** — the compiler is not the authority here; the spec
must be. So a BlockSpec carries:

- struct name, template params (with defaults), ctor signature, `may_block`
  (a trivial grep), port *names and types* (public, non-nested members only
  — private `_iq` buffers in the HackRF blocks are not ports; in-struct
  aliases like `using Sample = ...` resolved)
- **port-count authority**: `template_arg(k)` | `ctor_arg(k)` |
  `ctor_arg_len(k)` | `fixed(n)` | `unknown`

`connect`/`disconnect`/`set_arity` on a block whose authority is a ctor or
template arg patch that argument in the same atomic gesture. Lint compares
wired arity against the authority. `unknown` (e.g. `FusedBlock`, whose port
type is computed by metaprogramming) disables arity edits and static type
lint for that node — the escape hatch is read-only, never "user declares
ports by hand" (per-type manual ports are wrong for per-instance counts,
which is 8 of the 9 hard blocks).

Spec extraction runs over `desktop_blocks/**/*.hpp`, user-configured roots,
**and the open translation unit** — 7 of 22 examples define block structs
inline in the `.cpp`. Namespace-scope aliases (`SourceUHDBlockCF32`) list as
palette synonyms of one entry. Template args that name file-local
`constexpr` identifiers resolve through a file-local constant table; beyond
that, unresolved counts degrade to `unknown`, never to a guess.

## Rename and delete

- **Variable rename is cut from v1.** `rename_block` edits only the
  `BlockBase(name)` display string. Block variables are referenced all over
  the opaque region (`plot.render()`, 24 refs in `spike.cpp`); textual
  rename is unsafe and semantic rename is a compiler's job.
- **`remove_from_graph`** splices the runner out; the declaration stays.
- **`delete_block`** additionally removes the declaration, but first scans
  the whole translation unit for identifier nodes matching the variable;
  any hit outside the declaration and runner list → list them and require
  confirmation. Refusing honestly beats corrupting silently.

## Validation ladder

1. **Static lint** (session-side, live): edge channel-type mismatch where
   both specs are known, wired arity vs port-count authority, runner
   referencing an undeclared identifier, duplicate consumers on one channel.
   "Block without a runner" is a **warning** — a shipped example
   (`adsb_receiver.cpp` `null_sink`) legally does it.
2. **Compile**: shell out to the project's cmake; diagnostics stream to a
   panel. Authority for everything the spec model calls `unknown`.
3. **Run**: spawn the built binary (plot blocks open their own windows),
   stream output, SIGINT to stop.

The `origin/viz+linter` fixtures are **rewritten, not ported** — 4 of its 6
fail-fixtures don't actually fail (stub rules, never-populated type info)
and its `AddBlock` API predates the current one.

## Scope of v1

In: desktop flowgraph mode; multi-site files (render all, edit editable
ones); param/config/topology edits within the editable grammar; new-file
generation from a canonical template; static lint; compile-and-run panel.

Out: streamlined mode (no flowgraph call — read-only file), variable rename,
embedded targets, superblock authoring, editing read-only sites, merge
machinery for concurrent edits (revision guard + reload banner only),
post-construction setter calls in the param panel (ctor args only —
`set_frequency(...)` lines are opaque code).

## Repo layout

```
tools/flowgraph_gui/
  cler-graph/        Rust crate + CLI
  app/               Tauri shell + Svelte Flow frontend
```

Rust and node toolchains are confined here; never a dependency of the
framework. `tools/mermaid` is deleted at M1.

## Milestones

**M0 status: complete.** 160 tests, corpus 16/22 fully editable and 22/22
rendered, palette 39 structs with 0 wrong authorities and 1 Unknown
(`FusedBlock`), `spike.cpp` round-trip p95 14.5 ms against a 50 ms gate.
Three adversarial critics produced 47 findings against the first cut; all
confirmed defects are fixed and their attack tests live on, assertions
flipped, in `tests/regressions_{parse,palette,apply}.rs`. Corrections to
this document from that review: `desktop_blocks` holds **39** block structs,
not 36; `spike.cpp` parses fully editable (no read-only elements) so its M1
value is scale, not degradation; nine corpus files lack a trailing newline.

- **M0 — the crate, proven on the corpus.** `parse` emits a per-file status
  table for all 22 flowgraph examples: sites found, blocks, edges,
  editable/read-only per element — target 15/22 fully editable, 22/22
  correctly rendered, 0 misparsed as editable. `palette` reports per-block
  correctness over all 36 structs incl. port-count authority (target 36/36
  correct or explicitly `unknown`, never wrong). Property tests: apply
  random command → reparse equals model + byte-identity outside patched
  ranges; revision-mismatch rejection; atomic multi-range gestures.
  Round-trip benchmark on `spike.cpp`.
- **M1 — read-only canvas.** Open file, site selector, elk layout. Explicit
  acceptance cases: `mass_spring_damper.cpp` (cyclic), `plots.cpp` (parallel
  edges), `uhd_device.cpp` (four sites), `spike.cpp` (scale + read-only
  elements shown). Delete `tools/mermaid`.
- **M2 — param edits.** Ctor/template args, display name, direct config
  assignments. Commit-on-blur fields, undo, external-edit guard end to end.
- **M3 — topology edits.** Add from palette (declare-then-wire staging),
  connect/disconnect with arity-authority co-patching, remove-from-graph,
  delete-with-scan, live lint.
- **M4 — build & run.** Deferred until M2/M3 round-tripping is proven safe
  on real files; compiler diagnostics mapped to blocks where possible.

## Beyond M4 — the product roadmap

Direction: a **workbench**, not a diagram tool. The reference for interaction
design is Flux (flux.ai) — dark by default, an assistant living in a side
panel that proposes actions rather than prose, and a parts library with real
metadata. We take their interaction *patterns*, not their assets or code, and
the visual language stays ours (the rflock token set, Caribou red accent).

The single most important thing Flux gets right, and the reason it maps onto
this architecture so cleanly: **every action is explainable, reviewable and
reversible.** We already have that machinery — commands, validation, refusal
with reasons, undo, per-element editability. Everything below routes through
it. Nothing gets a privileged path to the file.

### M5 — typed edges

Colour each wire by the sample type flowing through it: `float`,
`std::complex<float>`, `Blob`, `uint16_t`, a user struct. The palette already
extracts port element types; what is missing is substituting a block's
template arguments into them, so `GainBlock<float>`'s `Channel<T>` resolves
to `float`. Positional substitution only — no semantic analysis, and an
unresolvable type takes a neutral colour rather than a guess, same rule as
everywhere else.

Two things fall out for free: the type-mismatch lint the validation ladder
already wants becomes real (both endpoints resolved and unequal → error on
the edge), and the canvas starts carrying DSP meaning at a glance — complex
baseband versus real audio is the distinction people actually care about.

Colours must come from a validated categorical palette (contrast against both
`--bg-0` and `--bg-1`, CVD-checked), not ad-hoc hues, and type must never be
encoded by colour alone — the port label stays.

### M6 — code affordances

Right-click a block: **view the source** of its declaration and its runner,
and **jump to it** in the user's editor at the exact line. Every element in
the model already carries byte spans, so this is nearly free and it is the
feature that keeps the GUI honest — the code is one click away, always, and
the user never has to wonder what the canvas did to their file.

Same menu: copy the block's C++ declaration, show the resolved template
arguments, reveal the header the type came from.

### M7 — block library

Promote the palette extractor into a browsable library: search, categories
(sources / sinks / math / plots / channelizers / resamplers / hardware),
`may_block` and port-count metadata visible, user-configured include roots
alongside `desktop_blocks/`, and a preview of the constructor signature
before you place anything. Blocks defined in the open file appear in it
automatically — they already do in the extractor.

### M8 — assistant panel

A chat panel that explains and proposes. Two hard rules:

1. **It emits commands, not text.** The assistant's output is the same
   command objects the canvas emits, applied through the same
   `DocumentSession` with the same validation, refusal reasons, atomicity and
   undo. It cannot write bytes directly, so it inherits every guarantee M0
   was hardened for. A proposed change renders as a diff to accept or reject.
2. **Explaining is the default, acting is opt-in.** "What does this
   channelizer do", "why is this edge read-only", "why is my graph not
   meeting the rate" are answerable from the model plus AGENTS.md and cost
   nothing but tokens. Structural edits need an explicit accept.

Needs a Claude API key, is off without one, and costs real money per
question — so it is last, and it is never on the path of anything else
working.

### Sequencing

M5 and M6 are cheap, land inside the existing architecture, and make the tool
visibly better — do them first, in that order. M7 is a UI investment over an
extractor that already works. M8 is the largest and the only one with an
external dependency and a running cost. None of them changes the one rule:
the `.cpp` is still the document.

## Standing risks

- The editable-grammar boundary is the product: if too many real files land
  read-only, the editor is a viewer. M0's status table is the go/no-go
  metric, produced before any GUI exists.
- Write-back fidelity remains the top hazard even after the revision guard;
  the property tests and the splice-only rule are the mitigations, and
  Normalize stays opt-in forever.
- webkit2gtk quirks on Linux; Svelte Flow is DOM-mainstream, risk low.
