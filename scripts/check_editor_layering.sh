#!/usr/bin/env bash
#
# Editor layering guards (editor/ARCHITECTURE.md "Dependency rules"), enforced as
# a source scan so the layering cannot silently regress. Companion to
# check_meshedit_deps.sh; same form (a ctest that fails on a violation).
#
# Kyusu is a set of workspace libraries over a shared library:
#   editor/common/src         what every workspace shares (editor_common)
#   editor/level_editor/src   the Level workspace
#
# Rules:
#
#   A. Core abstractions (common commands/ selection/ tools/ interaction/ and
#      level editor brush/) must not depend on the authoring/domain subsystems
#      (document/ viewport/ render/ ui/ editmodes/ meshedit/ workspace/). They
#      are the editor's reusable leaves; domain code depends on them, not the
#      reverse. The shared pointer-event header (input/InputEvent.h) and the
#      icon id leaf (icons/IconId.h) are the allowed crossings, so input/ and
#      icons/ are not in the forbidden set.
#
#   B. editor_common must not include an application-only subsystem. CMake
#      enforces this at compile time (application src dirs are not on common's
#      include path); this scan catches it earlier and names the file.
#
#   C. Only app/ (the composition root) may reach UP into workspace/ (the
#      aggregator). Cross-subsystem composition lives in workspace/; the
#      subsystems below it stay independent of it.
#
#   E. Workspaces never include one another. Each workspace tree (the level,
#      material, data, animation and UI preview editors, the project browser)
#      depends on editor_common and the engine only; the one tree that sees
#      them all is the Kyusu application, which composes them. A quoted include
#      that names a header existing only in another workspace tree is a
#      violation.
#
#   G. The shell is a replaceable presentation policy
#      (docs/editor/interaction-grammar.md). No workspace tree includes
#      common/shell/: a workspace says what it has (actions, properties,
#      resources, facets) and asks for surfaces by role, and only the Kyusu
#      application and the shell itself decide how any of it appears.
#
#   F. No file under editor/ names ImGui (its headers, its namespace, its
#      types). The editor presents through Shoji; the engine's own diagnostics
#      overlay is the only ImGui in a Kyusu window.
#
# Usage: check_editor_layering.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
COMMON="$ROOT/editor/common/src"
LEVEL="$ROOT/editor/level_editor/src"
status=0

# Greps for a pattern but drops comment-only lines, so prose mentioning a
# forbidden name is not flagged.
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

# A. Core abstractions must not include a domain/authoring subsystem. The
# optional path prefix makes this catch relative includes ("../document/...").
check "core abstraction depends on a domain subsystem (only input/InputEvent.h may cross)" \
      '#include[[:space:]]*["<]([^">]*/)?(document|viewport|render|ui|editmodes|meshedit|workspace)/' \
      "$COMMON/commands" "$COMMON/selection" "$COMMON/tools" "$COMMON/interaction" "$LEVEL/brush"

# B. The shared shell must not include a subsystem that exists only inside an
# application tree. Quoted includes only: angle includes name engine headers
# (the engine also has an app/ include dir), editor-internal includes are quoted.
check "editor_common depends on an application-only subsystem" \
      '#include[[:space:]]*"([^"]*/)?(app|brush|document|editmodes|export|meshedit|overlay|workspace)/' \
      "$COMMON"

# C. Only app/ may include workspace/. Filter on the including FILE's path (not
# the line text, which contains "workspace/" by construction), so a real
# violation in content is not masked.
ws_includers="$(grep -rlE '#include[[:space:]]*["<]([^">]*/)?workspace/' "$COMMON" "$LEVEL" 2>/dev/null \
                | grep -vE '^'"$LEVEL"'/(app|workspace)/')"
if [ -n "$ws_includers" ]; then
    echo "VIOLATION: a subsystem below the aggregator reaches up into workspace/ (compose in app/ instead)"
    echo "$ws_includers"
    echo
    status=1
fi

# E. No workspace tree includes another's headers. An include resolves against
# its own tree or editor_common first; one that only another tree can satisfy
# is a reach across workspaces.
WORKSPACES="level_editor material_editor ui_preview data_editor animation_editor project_browser"
for ws in $WORKSPACES; do
    own="$ROOT/editor/$ws/src"
    [ -d "$own" ] || continue
    while IFS= read -r hit; do
        file="${hit%%:*}"
        header="$(printf '%s\n' "$hit" | sed -E 's/.*#include[[:space:]]*"([^"]+)".*/\1/')"
        [ -f "$own/$header" ] && continue
        [ -f "$COMMON/$header" ] && continue
        [ -f "$(dirname "$file")/$header" ] && continue
        for other in $WORKSPACES; do
            [ "$other" = "$ws" ] && continue
            if [ -f "$ROOT/editor/$other/src/$header" ]; then
                echo "VIOLATION: workspace $ws includes $other's header (compose workspaces in editor/kyusu instead)"
                echo "$hit"
                echo
                status=1
            fi
        done
    done < <(grep -rnE '^[[:space:]]*#include[[:space:]]*"' "$own" 2>/dev/null)
done

# G. Workspaces never include the shell's policy.
for ws in $WORKSPACES; do
    own="$ROOT/editor/$ws/src"
    [ -d "$own" ] || continue
    check "workspace $ws includes the shell's presentation policy (expose semantics; the shell presents them)" \
          '#include[[:space:]]*"([^"]*/)?shell/' "$own"
done

# F. No ImGui. Comment-only lines are ignored, like every rule here.
imgui_pattern='(#include[[:space:]]*[<"][^">]*imgui|\bIm(Gui|Vec2|Vec4|TextureID|DrawList|U32|Font|Wchar)\b|ImGui::|ImGui[A-Z][A-Za-z]*Flags)'
imgui_users="$(cd "$ROOT" && grep -rnE "$imgui_pattern" editor --include='*.h' --include='*.cpp' 2>/dev/null \
               | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' | cut -d: -f1 | sort -u)"
if [ -n "$imgui_users" ]; then
    echo "VIOLATION: ImGui under editor/ (present it through Shoji instead)"
    echo "$imgui_users"
    echo
    status=1
fi
if [ "$status" -eq 0 ]; then
    echo "editor layering directions: OK"
fi
exit "$status"
