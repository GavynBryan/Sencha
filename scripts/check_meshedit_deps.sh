#!/usr/bin/env bash
#
# Enforces the mesh-edit subsystem's dependency directions (docs/plans
# mesh-edit subsystem, "Dependency Rules"). Fails (exit 1) if any layering
# violation is found, so the layering can't silently regress.
#
# Usage: check_meshedit_deps.sh <source-root>
#
# The three rules checked:
#   1. UI / viewport / render / editmodes must not include BrushOps.
#   2. editor/meshedit/ must not depend on scene / UI / render / viewport.
#   3. The retired BrushGeometry face-projection API must not be referenced.
#      (MeshElements::TryGetFace is the supported replacement and is allowed.)

set -uo pipefail

ROOT="${1:-.}"
EDITOR="$ROOT/editor"
status=0

# Greps for a pattern but drops lines that are purely comments, so prose that
# mentions a forbidden name (e.g. "knows nothing about BrushOps") isn't flagged.
check() {
    local desc="$1" pattern="$2"
    shift 2
    local hits
    hits="$(grep -rnE "$pattern" "$@" 2>/dev/null \
            | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)')"
    if [ -n "$hits" ]; then
        echo "VIOLATION: $desc"
        echo "$hits"
        echo
        status=1
    fi
}

check "UI/viewport/render/editmodes include BrushOps" \
      '#include[[:space:]]*["<].*BrushOps' \
      "$EDITOR/ui" "$EDITOR/viewport" "$EDITOR/render" "$EDITOR/editmodes"

check "editor/meshedit depends on scene/UI/render/viewport" \
      'LevelScene|LevelDocument|imgui|Renderer|EditorViewport' \
      "$EDITOR/meshedit"

check "retired BrushGeometry face-projection API is referenced" \
      'EnumerateFaces|BrushFaceDescriptor|BrushFaceGeometry' \
      "$EDITOR"

if [ "$status" -eq 0 ]; then
    echo "mesh-edit dependency directions: OK"
fi
exit "$status"
