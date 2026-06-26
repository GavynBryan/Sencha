#!/usr/bin/env bash
#
# Fitness function: the game-module ABI surface stays free of tool/GUI/render
# concerns. The headers a game module compiles against (and whose layout its
# binary bakes in) must describe data and registration only — never ImGui, the
# render backend, or the editor. A leak here both bloats the module ABI and
# couples tooling evolution to module binaries (the EditorVisual-on-the-serializer
# lesson). (docs/architecture/hardening-and-consolidation.md W6.)
#
# Usage: check_module_abi.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
INCLUDE="$ROOT/engine/include"
status=0

# The module-facing ABI header dirs — the same surface the fingerprint hashes.
ABI_DIRS=(
    "$INCLUDE/app"
    "$INCLUDE/world/serialization"
    "$INCLUDE/core/metadata"
    "$INCLUDE/core/console"
    "$INCLUDE/ecs"
)

# Greps the ABI dirs for a pattern, dropping comment-only lines so prose can't
# trip it.
hits="$(grep -rnE '#include[[:space:]]*[<"](imgui|editor/|graphics/|vulkan)' "${ABI_DIRS[@]}" 2>/dev/null \
        | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)')"

if [ -n "$hits" ]; then
    echo "VIOLATION: tool/GUI/render include in the module ABI surface"
    echo "$hits"
    echo
    echo "Module-ABI headers must stay data/registration only. Move the concern"
    echo "to the editor/tool side (e.g. carry an asset path as data, not a type)."
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "module ABI surface: OK"
fi
exit "$status"
