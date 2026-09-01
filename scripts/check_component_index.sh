#!/usr/bin/env bash
# The cross-header half of component metadata validation: a stable identity,
# schema name or scene chunk claimed twice must fail the build, naming both
# declarations. Per-header generation cannot see these, so nothing else does.
set -uo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
VALIDATOR="${ROOT}/cmake/ValidateComponentIndex.cmake"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

status=0

record() { printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" "$5" "$6"; }

expect() {
    local want="$1" name="$2"
    shift 2
    cmake -DINDEX_DIR="${WORK}/idx" -P "${VALIDATOR}" >"${WORK}/out" 2>&1
    local got=$?
    if [[ "${want}" == pass && ${got} -ne 0 ]]; then
        echo "FAIL: ${name}: expected acceptance, got:"; sed 's/^/    /' "${WORK}/out"
        status=1
    elif [[ "${want}" == fail && ${got} -eq 0 ]]; then
        echo "FAIL: ${name}: expected rejection, was accepted"
        status=1
    elif [[ "${want}" == fail ]] && ! grep -q "already claimed by" "${WORK}/out"; then
        echo "FAIL: ${name}: rejected without naming the prior claim"; sed 's/^/    /' "${WORK}/out"
        status=1
    fi
}

reset() { rm -rf "${WORK}/idx"; mkdir -p "${WORK}/idx"; }

reset
record LocalTransform Transform Transform XFRM world/transform/TransformComponents.h 52 > "${WORK}/idx/a.index"
record CameraComponent Camera Camera CAMR components/CameraComponent.h 40 > "${WORK}/idx/b.index"
expect pass "distinct components"

record Impostor Transform Impostor IMPO game/Impostor.h 9 > "${WORK}/idx/c.index"
expect fail "duplicate identity"

reset
record LocalTransform Transform Transform XFRM world/transform/TransformComponents.h 52 > "${WORK}/idx/a.index"
record Other other Transform OTHR game/Other.h 3 > "${WORK}/idx/b.index"
expect fail "duplicate schema name"

reset
record LocalTransform Transform Transform XFRM world/transform/TransformComponents.h 52 > "${WORK}/idx/a.index"
record Other other Other XFRM game/Other.h 3 > "${WORK}/idx/b.index"
expect fail "duplicate scene chunk"

# A component that is authored but never persisted declares no chunk, and any
# number of those must coexist.
reset
record TuningA a A "" movement/A.h 1 > "${WORK}/idx/a.index"
record TuningB b B "" movement/B.h 1 > "${WORK}/idx/b.index"
expect pass "components with no scene chunk"

if [[ ${status} -eq 0 ]]; then
    echo "component index validation: OK"
fi
exit ${status}
