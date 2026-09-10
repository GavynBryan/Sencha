#!/usr/bin/env bash
#
# Editor layering guards (editor/ARCHITECTURE.md "Dependency rules"), enforced as
# a source scan so the layering cannot silently regress. Companion to
# check_meshedit_deps.sh; same form (a ctest that fails on a violation).
#
# The editor is a family of applications over a shared shell:
#   editor/common/src  the shared editor shell (editor_common)
#   editor/kyusu/src   the level editor application
#
# Three rules:
#
#   A. Core abstractions (common commands/ selection/ tools/ interaction/ and
#      kyusu brush/) must not depend on the authoring/domain subsystems
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
#   D. The chrome's internals (its geometry, painters, frame, chassis,
#      ornaments, sources, icon drawing) are reachable only from inside
#      common/ui/. A panel asks for a PanelStyle, a section title, a selection
#      scope, a control; it never draws a screw or a chamfer itself, so the
#      look can change without a panel changing.
#
# Usage: check_editor_layering.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
COMMON="$ROOT/editor/common/src"
KYUSU="$ROOT/editor/kyusu/src"
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
      "$COMMON/commands" "$COMMON/selection" "$COMMON/tools" "$COMMON/interaction" "$KYUSU/brush"

# B. The shared shell must not include a subsystem that exists only inside an
# application tree. Quoted includes only: angle includes name engine headers
# (the engine also has an app/ include dir), editor-internal includes are quoted.
check "editor_common depends on an application-only subsystem" \
      '#include[[:space:]]*"([^"]*/)?(app|brush|document|editmodes|export|meshedit|overlay|workspace)/' \
      "$COMMON"

# C. Only app/ may include workspace/, with one deliberate exception: panels may
# read the narrow workspace mechanisms they drive (a panel that needs a pending
# edit should hold that mechanism, not a bag of callbacks assembled for it).
# EditorWorkspace itself stays off-limits to ui/ -- a panel takes the mechanism,
# never the aggregator. Filter on the including FILE's path (not the line text,
# which contains "workspace/" by construction), so a real violation in content
# is not masked.
ws_panel_mechanisms='workspace/(PendingBridgeEdit|PendingElementEdit|SelectionActions)\.h'
ws_includers="$(grep -rlE '#include[[:space:]]*["<]([^">]*/)?workspace/' "$COMMON" "$KYUSU" 2>/dev/null \
                | grep -vE '^'"$KYUSU"'/(app|workspace)/')"
for file in $ws_includers; do
    # A ui/ file is clean only when EVERY workspace include it has is an allowed
    # mechanism; one stray aggregator include still fails.
    if [ "${file#"$KYUSU"/ui/}" != "$file" ] \
       && ! grep -E '#include[[:space:]]*["<]([^">]*/)?workspace/' "$file" \
            | grep -qvE "$ws_panel_mechanisms"; then
        continue
    fi
    echo "VIOLATION: a subsystem below the aggregator reaches up into workspace/ (compose in app/ instead)"
    echo "$file"
    echo
    status=1
done

# D. Chrome internals stay inside common/ui/. Filter on the including FILE's
# path, as in rule C.
chrome_internals='ui/chrome/(ChromeGeometry|ChromePaint|ChromeFrame|ChromeChassis|ChromeOrnaments|IconDraw)\.h'
chrome_includers="$(grep -rlE '#include[[:space:]]*["<]([^">]*/)?'"$chrome_internals" "$COMMON" "$KYUSU" 2>/dev/null \
                    | grep -vE '^'"$COMMON"'/ui/')"
if [ -n "$chrome_includers" ]; then
    echo "VIOLATION: a panel or subsystem draws chrome internals (use ScopedPanel, ChromeHeader, ChromeSelection, ChromeControls, ChromeDecor, ChromeBars)"
    echo "$chrome_includers"
    echo
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "editor layering directions: OK"
fi
exit "$status"
