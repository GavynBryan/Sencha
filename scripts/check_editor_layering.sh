#!/usr/bin/env bash
#
# Editor layering guards (editor/ARCHITECTURE.md "Dependency rules"), enforced as
# a source scan so the layering cannot silently regress. Companion to
# check_meshedit_deps.sh; same form (a ctest that fails on a violation).
#
# Two rules, both green when introduced:
#
#   A. Core abstractions (commands/ selection/ tools/ interaction/ brush/) must
#      not depend on the authoring/domain subsystems (document/ viewport/ render/
#      ui/ editmodes/ meshedit/ workspace/). They are the editor's reusable
#      leaves; domain code depends on them, not the reverse. The shared
#      pointer-event header (input/InputEvent.h) is the one allowed crossing, so
#      input/ is not in the forbidden set.
#
#   B. Only app/ (the composition root) may reach UP into workspace/ (the
#      aggregator). Cross-subsystem composition lives in workspace/; the
#      subsystems below it stay independent of it.
#
# Usage: check_editor_layering.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
EDITOR="$ROOT/editor/src"
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
      "$EDITOR/commands" "$EDITOR/selection" "$EDITOR/tools" "$EDITOR/interaction" "$EDITOR/brush"

# B. Only app/ may include workspace/. Filter on the including FILE's path (not
# the line text, which contains "workspace/" by construction), so a real
# violation in content is not masked.
ws_includers="$(grep -rlE '#include[[:space:]]*["<]([^">]*/)?workspace/' "$EDITOR" 2>/dev/null \
                | grep -vE '^'"$EDITOR"'/(app|workspace)/')"
if [ -n "$ws_includers" ]; then
    echo "VIOLATION: a subsystem below the aggregator reaches up into workspace/ (compose in app/ instead)"
    echo "$ws_includers"
    echo
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "editor layering directions: OK"
fi
exit "$status"
