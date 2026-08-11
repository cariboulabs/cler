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
  "build": {}
}
```

The top-level sections are independent namespaces. Readers must ignore unknown
members, and writers updating one namespace must preserve unknown members and
the other namespaces. Additive changes keep the current version. Increment
`version` only when an existing field changes meaning or representation.

The draft is reusable only while `document.savedSha256` matches the source file.
If it does not match, the source file becomes the new baseline while reusable UI
state remains cached. Temporary-directory cleanup may remove the entire cache at
any time.

The JSON state is platform-neutral. Cached build artifacts are local to the
host toolchain and are recreated as needed on Linux, macOS, or Windows through
WSL2; Cler does not support native Windows builds.
