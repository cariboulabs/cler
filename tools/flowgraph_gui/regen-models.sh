#!/usr/bin/env bash
# Refresh the vendored model catalog from models.dev (MIT, github.com/sst/models.dev).
# Keeps only the providers the AI agent can talk to, and only tool-capable models —
# the agent proposes changes through a tool call, so a model without one is useless here.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$here/app/src-tauri/models.json"

curl -sSf --max-time 60 https://models.dev/api.json | python3 -c '
import json, sys

WANTED = {"anthropic": "anthropic", "openai": "openai"}
catalog = json.load(sys.stdin)
out = {}
for source, provider in WANTED.items():
    models = catalog.get(source, {}).get("models", {})
    listed = []
    for model in models.values():
        if not model.get("tool_call"):
            continue
        limit = model.get("limit") or {}
        cost = model.get("cost") or {}
        listed.append({
            "id": model["id"],
            "name": model.get("name", model["id"]),
            "context": limit.get("context", 0),
            "output": limit.get("output", 0),
            "input_cost": cost.get("input", 0),
            "output_cost": cost.get("output", 0),
            "released": model.get("release_date", ""),
        })
    listed.sort(key=lambda one: (one["released"], one["id"]), reverse=True)
    out[provider] = listed

json.dump(out, sys.stdout, indent=2, sort_keys=False)
sys.stdout.write("\n")
' > "$out"

printf 'wrote %s (%s bytes)\n' "$out" "$(wc -c < "$out")"
