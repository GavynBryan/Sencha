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
    cmake -DINDEX_DIR="${WORK}/idx" "$@" -P "${VALIDATOR}" >"${WORK}/out" 2>&1
    local got=$?
    # CMake wraps a long message at its own discretion.
    tr -s '[:space:]' ' ' <"${WORK}/out" >"${WORK}/out.flat"
    if [[ "${want}" == pass && ${got} -ne 0 ]]; then
        echo "FAIL: ${name}: expected acceptance, got:"; sed 's/^/    /' "${WORK}/out"
        status=1
    elif [[ "${want}" == fail && ${got} -eq 0 ]]; then
        echo "FAIL: ${name}: expected rejection, was accepted"
        status=1
    elif [[ "${want}" == fail ]] && ! grep -q "already claimed by" "${WORK}/out.flat"; then
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

# The aggregate is the target's own roster, sorted, with the base it was
# checked against left out.
reset
record LocalTransform Transform Transform XFRM world/transform/TransformComponents.h 52 > "${WORK}/idx/a.index"
record CameraComponent Camera Camera CAMR components/CameraComponent.h 40 > "${WORK}/idx/b.index"
record Collider sencha.physics.collider "" "" physics/components/Collider.h 15 > "${WORK}/base.index"
expect pass "aggregate" -DBASE_INDEX="${WORK}/base.index" -DOUTPUT="${WORK}/all.index"
{
    record CameraComponent Camera Camera CAMR components/CameraComponent.h 40
    record LocalTransform Transform Transform XFRM world/transform/TransformComponents.h 52
} > "${WORK}/want.index"
if ! cmp -s "${WORK}/all.index" "${WORK}/want.index"; then
    echo "FAIL: aggregate: unexpected content:"; sed 's/^/    /' "${WORK}/all.index"
    status=1
fi

# A collision with the base names the base declaration as the prior claim,
# since that is the one a module cannot change.
record Impostor sencha.physics.collider Impostor IMPO game/Impostor.h 9 > "${WORK}/idx/c.index"
expect fail "collision with the base index" -DBASE_INDEX="${WORK}/base.index"
if ! grep -q "already claimed by Collider (physics/components/Collider.h:15)" "${WORK}/out.flat"; then
    echo "FAIL: collision with the base index: prior claim is not the base's:"; sed 's/^/    /' "${WORK}/out"
    status=1
fi

if [[ ${status} -eq 0 ]]; then
    echo "component index validation: OK"
fi
exit ${status}
