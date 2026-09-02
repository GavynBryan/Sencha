#!/usr/bin/env bash
#
# Components are data (CLAUDE.md, ECS rules). A component header declares the
# rows a system reads and the handles they carry -- nothing that could only be
# used by reaching out of the row.
#
# Enforced as a source scan because the compiler cannot state it: including
# ecs/World.h in a component header compiles fine and quietly makes every
# consumer of that component depend on the world, which is how a component
# starts growing behaviour. A concrete cache in the same place says the row
# owns a resource rather than a handle to one, and a graphics header says the
# row names a GPU object -- both of which relocatable component storage cannot
# hold. Component headers are also the ABI surface a game module compiles
# against, so what they drag in is what a module drags in.
#
# The rosters are the ones the build already keeps: SENCHA_ENGINE_COMPONENT_HEADERS
# and SENCHA_TEMPLATE_COMPONENT_HEADERS, which is what the codegen runs over.
#
# Usage: check_component_headers.sh <source-root>

set -uo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
status=0

# Pulls one CMake list's entries, resolving the variable each roster spells its
# paths with.
roster() {
    local file="$1" var="$2" prefix="$3"
    awk -v var="set(${var}" '
        index($0, var) == 1 { inside = 1 }
        inside { print }
        inside && /\)/ { exit }
    ' "${file}" \
        | grep -oE '"[^"]+"|[A-Za-z0-9_./-]+\.h' \
        | tr -d '"' \
        | sed "s|\${SENCHA_ENGINE_INCLUDE_DIR}|${prefix}|"
}

HEADERS="$(roster "${ROOT}/engine/CMakeLists.txt" SENCHA_ENGINE_COMPONENT_HEADERS \
                  "${ROOT}/engine/include")"
HEADERS+=$'\n'"$(roster "${ROOT}/template/CMakeLists.txt" SENCHA_TEMPLATE_COMPONENT_HEADERS \
                        "${ROOT}/template" | sed "s|^src/|${ROOT}/template/src/|")"

count=0
while read -r header; do
    [ -n "${header}" ] || continue
    if [ ! -f "${header}" ]; then
        echo "VIOLATION: component roster names a header that is not there: ${header}"
        status=1
        continue
    fi
    count=$((count + 1))
    hits="$(grep -nE '#include[[:space:]]*[<"](ecs/World\.h|graphics/|([^">]*/)?[A-Za-z]*Cache\.h)' \
                 "${header}" 2>/dev/null)"
    if [ -n "${hits}" ]; then
        echo "VIOLATION: ${header#"${ROOT}"/} reaches past the row it declares"
        echo "${hits}" | sed 's/^/    /'
        status=1
    fi
done <<< "${HEADERS}"

if [ "${count}" -lt 20 ]; then
    echo "VIOLATION: only ${count} component headers found; the roster parse is wrong"
    status=1
fi

if [ "${status}" -eq 0 ]; then
    echo "component headers (${count}) carry data only: OK"
fi
exit "${status}"
