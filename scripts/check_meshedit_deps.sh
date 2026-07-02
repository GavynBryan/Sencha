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
COMMON="$ROOT/editor/common/src"
KYUSU="$ROOT/editor/kyusu/src"
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
      "$COMMON/ui" "$COMMON/render" \
      "$KYUSU/ui" "$KYUSU/viewport" "$KYUSU/render" "$KYUSU/editmodes"

check "editor/meshedit depends on scene/UI/render/viewport" \
      'EditorScene|EditorDocument|imgui|Renderer|EditorViewport' \
      "$KYUSU/meshedit"

check "retired BrushGeometry face-projection API is referenced" \
      'EnumerateFaces|BrushFaceDescriptor|BrushFaceGeometry' \
      "$COMMON" "$KYUSU"

check "editor/editmodes depends on the scene (must go through ManipulationSink)" \
      'EditorScene|EditorDocument' \
      "$KYUSU/editmodes"

# The ManipulationSink must only be implemented under editor/workspace (the lone
# scene-mutation seam). An implementor anywhere else breaks the layering.
sink_impls="$(grep -rlE 'public[[:space:]]+ManipulationSink' "$COMMON" "$KYUSU" 2>/dev/null \
              | grep -vE '^'"$KYUSU"'/workspace/')"
if [ -n "$sink_impls" ]; then
    echo "VIOLATION: ManipulationSink implemented outside editor/workspace"
    echo "$sink_impls"
    echo
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "mesh-edit dependency directions: OK"
fi
exit "$status"
