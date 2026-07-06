#!/usr/bin/env bash
#
# T scripting layering guards (docs/plans/t-language-v1-spec.md), enforced as a
# source scan so the dependency direction cannot silently regress. Same form as
# check_editor_layering.sh: a ctest that fails on a violation.
#
# Two rules:
#
#   A. The VM core (bytecode, module container, validator, interpreter,
#      intrinsics, compiler) is world-agnostic. It must not include upward into
#      the engine's gameplay/runtime layers (ecs/, world/, app/, abilities/,
#      physics/, gameplay_tags/, render/, audio/, jobs/, runtime/, zone/,
#      movement/). It depends only on core/. The world-side bridges
#      (ScriptLink, WorldScriptHost, ScriptBehaviorSystem, ScriptRuntime) are
#      the seam that reaches those layers and are exempt.
#
#   B. The compiler translation units are cook/dev-time; nothing in the runtime
#      VM core includes the compiler except through the ScriptCompiler.h facade.
#
# Usage: check_script_layering.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
INC="$ROOT/engine/include/script"
SRC="$ROOT/engine/src/script"
status=0

# VM-core files: everything under script/ EXCEPT the bridge files.
core_files() {
    { find "$INC" "$SRC" -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null; } \
        | grep -vE '(ScriptRuntime|ScriptLink|WorldScriptHost|ScriptBehaviorSystem|ScriptAbilitySystem|ScriptSceneLink|ScriptComponents)\.' \
        | grep -vE '(ScriptCook)\.'
}

# A. No upward includes from the VM core.
upward='#include[[:space:]]*["<]([^">]*/)?(ecs|world|app|abilities|physics|gameplay_tags|render|audio|jobs|runtime|zone|movement|effects|attributes)/'
hits="$(core_files | while read -r f; do
            grep -nE "$upward" "$f" 2>/dev/null \
                | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' \
                | sed "s|^|$f:|"
        done)"
if [ -n "$hits" ]; then
    echo "VIOLATION: VM core includes an upward gameplay/runtime layer (bridges are the only seam)"
    echo "$hits"
    echo
    status=1
fi

# B. The compiler is reachable only through ScriptCompiler.h. No VM-core file
# (other than that facade) includes a compiler/ internal header.
compiler_leak='#include[[:space:]]*["<]([^">]*/)?compiler/'
hits="$(core_files | grep -vE 'ScriptCompiler\.h' | while read -r f; do
            grep -nE "$compiler_leak" "$f" 2>/dev/null \
                | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' \
                | sed "s|^|$f:|"
        done)"
if [ -n "$hits" ]; then
    echo "VIOLATION: a VM-core file reaches into a compiler/ internal header"
    echo "$hits"
    echo
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "check_script_layering: OK"
fi
exit "$status"
