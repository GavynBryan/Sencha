#!/usr/bin/env bash
#
# T scripting determinism-surface guard (docs/plans/t-language-v1-spec.md,
# section D). The VM must be deterministic by construction: the only way a
# script reaches the outside world is the declared host API. This scan fails if
# a nondeterminism source appears anywhere under the VM core: wall-clock time,
# filesystem or socket IO, or thread/async primitives.
#
# The compiler and bridges are exempt where they legitimately need these:
#   - the compiler resolver and the cook importer read source files (dev-time,
#     not runtime execution), and
#   - the bridges use the engine's own scheduled-system machinery.
# This guard targets the runtime VM core (interpreter, module, validator,
# intrinsics), which must have none of these.
#
# Usage: check_script_host_surface.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
INC="$ROOT/engine/include/script"
SRC="$ROOT/engine/src/script"
status=0

# Runtime VM-core files only: exclude the compiler, the cook importer, and the
# world-side bridges (all dev-time or engine-mediated).
runtime_core() {
    { find "$INC" "$SRC" -maxdepth 1 -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null; } \
        | grep -vE '(ScriptRuntime|ScriptLink|WorldScriptHost|ScriptBehaviorSystem|ScriptAbilitySystem|ScriptSceneLink|ScriptComponents|ScriptComponentSerializer|ScriptCook|ScriptCompiler)\.'
}

# Forbidden symbols: wall clock, IO streams, sockets, threads/async. Word
# boundaries keep this from matching unrelated identifiers.
forbidden='(std::chrono|std::this_thread|std::thread|std::async|std::mutex|steady_clock|system_clock|high_resolution_clock|std::ifstream|std::ofstream|std::fstream|std::fopen|::socket\(|<thread>|<chrono>|<mutex>|<fstream>)'

hits="$(runtime_core | while read -r f; do
            grep -nE "$forbidden" "$f" 2>/dev/null \
                | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' \
                | sed "s|^|$f:|"
        done)"
if [ -n "$hits" ]; then
    echo "VIOLATION: a nondeterminism source appears in the VM runtime core"
    echo "$hits"
    echo
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "check_script_host_surface: OK"
fi
exit "$status"
