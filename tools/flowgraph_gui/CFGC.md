# Cler Flowgraph Cache

`.cfgc` is a disposable, versioned JSON cache for the flowgraph GUI. It is not
source code or a project file. The desktop app stores it in the current user's
private temporary directory beside the draft source and shadow build.

```json
{
  "format": "cler-flowgraph-cache",
  "version": 1,
  "document": {
    "sourcePath": "/path/to/flowgraph.cpp",
    "savedSha256": "..."
  },
  "ui": {
    "version": 1,
    "activeView": "main#0",
    "views": {},
    "panels": {}
  },
  "build": {
    "version": 1,
    "artifacts": {
      "cmake:desktop_examples/hello_world.cpp:hello_world": {
        "inputKey": {
          "inputs": {
            "draft": "...",
            "cmake": "...",
            "dependency:repo:include/cler.hpp": "..."
          },
          "recipeSha256": "..."
        },
        "producer": "cmake",
        "artifactPath": "/tmp/.../build/desktop_examples/hello_world",
        "completedUnixMs": 0
      }
    }
  }
}
```

The top-level sections are independent namespaces. Readers must ignore unknown
members, and writers updating one namespace must preserve unknown members and
the other namespaces. Additive changes keep the current version. Increment
`version` only when an existing field changes meaning or representation.

`build.artifacts` is a named provenance catalog, not a single build flag. Each
producer records the inputs and recipe that created an artifact. Input names are
additive so future producers can include generated code, headers, toolchains, or
other dependencies without changing the UI contract. Run is allowed only when
the backend finds a matching record and executable; the frontend only presents
that backend-derived state. CMake artifacts include compiler-discovered project
dependencies and project CMake files. Active builds are runtime state shared by
all windows and are not persisted.

The draft is reusable only while `document.savedSha256` matches the source file.
If it does not match, the source file becomes the new baseline while reusable UI
state remains cached. Temporary-directory cleanup may remove the entire cache at
any time. The app also prunes missing artifact records and keeps a bounded recent
snapshot set; recent snapshots receive an age grace period so cleanup cannot
invalidate an active compiler.

The JSON state is platform-neutral. Cached build artifacts are local to the
host toolchain and are recreated as needed on Linux, macOS, or Windows through
WSL2; Cler does not support native Windows builds.
