#!/usr/bin/env bash
# The generator's own contract: what it emits for the golden component and the
# golden authored API, that it
# emits the same bytes twice, that a malformed declaration is refused without
# leaving output behind -- an empty companion would read as a component that no
# longer exists -- and that the format it reports is the one the headers read.
#
# Some rules are the companion's to enforce rather than the generator's --
# whether a type named as a target is a component is known to the compiler,
# not to a header parse -- so one fixture's companion is compiled, and must
# fail to, naming the declaration.
#
# A companion is regenerated when a header its header includes changes, since a
# default naming another header's constant is baked into it: a small project
# built with the codegen CMake function proves it, given CMake and a generator.
#
#   check_component_codegen.sh <tool> <source-root> <generated-public-dir> [<c++ compiler>
#                              [<cmake> <cmake generator> <make program>]]
set -uo pipefail

TOOL="${1:?tool path required}"
ROOT="${2:?source root required}"
GENERATED="${3:?generated-public dir required}"
CXX="${4:-}"
CMAKE="${5:-}"
CMAKE_GENERATOR_NAME="${6:-}"
MAKE_PROGRAM="${7:-}"

FIXTURES="${ROOT}/test/component_codegen"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

printf -- '-std=c++20\n-DSENCHA_CODEGEN=1\n-Wno-pragma-once-outside-header\n-I%s/engine/include\n-I%s\n' \
    "${ROOT}" "${GENERATED}" > "${WORK}/flags"

status=0

run() {
    local header="$1" out="$2"
    "${TOOL}" "${FIXTURES}/${header}" \
        --output="${WORK}/${out}.h" --index="${WORK}/${out}.index" \
        --logical="test/component_codegen/${header}" --flags="${WORK}/flags" \
        >"${WORK}/${out}.log" 2>&1
}

expect_rejected() {
    local header="$1" name="$2" pattern="$3"
    if run "${header}" "${name}"; then
        echo "FAIL: ${name}: expected rejection, was accepted"
        status=1
    elif ! grep -q "${pattern}" "${WORK}/${name}.log"; then
        echo "FAIL: ${name}: rejected without explaining why"
        sed 's/^/    /' "${WORK}/${name}.log"
        status=1
    elif [[ -e "${WORK}/${name}.h" ]]; then
        echo "FAIL: ${name}: wrote a companion despite failing"
        status=1
    fi
}

if ! run GoldenComponent.h golden; then
    echo "FAIL: the golden component did not generate:"
    sed 's/^/    /' "${WORK}/golden.log"
    exit 1
fi

if ! diff -u "${FIXTURES}/GoldenComponent.sencha.h.expected" "${WORK}/golden.h"; then
    echo "FAIL: generated metadata differs from the golden expectation"
    status=1
fi

if ! run GoldenAuthoredApi.h golden_api; then
    echo "FAIL: the golden authored API did not generate:"
    sed 's/^/    /' "${WORK}/golden_api.log"
    exit 1
fi

if ! diff -u "${FIXTURES}/GoldenAuthoredApi.sencha.h.expected" "${WORK}/golden_api.h"; then
    echo "FAIL: generated authored API differs from the golden expectation"
    status=1
fi

run GoldenComponent.h golden_again
if ! diff -q "${WORK}/golden.h" "${WORK}/golden_again.h" >/dev/null \
   || ! diff -q "${WORK}/golden.index" "${WORK}/golden_again.index" >/dev/null; then
    echo "FAIL: two runs produced different output"
    status=1
fi

expect_rejected BadPredictedNotReplicated.h predicted "predicted but not replicated"

# Authored declarations are refused where they are written, naming the rule.
expect_rejected BadTargetType.h target_type "uses SENCHA_TARGET(BadDoor) but has type float"
expect_rejected BadUnannotatedParameter.h unannotated "needs exactly one of SENCHA_ARG and SENCHA_TARGET"
expect_rejected BadMutableQuery.h mutable_query "must be const"
expect_rejected BadVerbReturn.h verb_return "a verb returns VerbAdmission, VerbAnswer, VerbRefusal<E> or VerbResult<T, E>"
expect_rejected BadQueryOnPlainStruct.h plain_query "is not a SENCHA_COMPONENT"
expect_rejected BadDuplicateIdentity.h duplicate_identity "is declared twice"
expect_rejected BadOptionalParameter.h optional_parameter "is declared std::optional<T>"
expect_rejected BadNonConstantDefault.h non_constant_default "is not a constant"
expect_rejected BadComponentEvent.h component_event "both a component and an event"
expect_rejected BadComponentMethod.h component_method "components and events are data"
expect_rejected BadNoIdentity.h no_identity "no SENCHA_COMPONENT identity"
expect_rejected BadShortChunk.h short_chunk "exactly four characters"
expect_rejected BadIconNotPlaceable.h icon_not_placeable "names a place icon but is not SENCHA_PLACEABLE"
expect_rejected BadRuntimeNotSaved.h runtime_not_saved "is not SENCHA_SAVED, so no save holds it"
expect_rejected BadRuntimeAndUnsaved.h runtime_and_unsaved "is both SENCHA_RUNTIME and SENCHA_UNSAVED"
expect_rejected BadRuntimeWithoutDefault.h runtime_without_default "has no member initializer"
expect_rejected BadLeaseWithoutRelease.h lease_without_release "the lease has no SENCHA_LEASE_RELEASE"
expect_rejected BadCompletesWithoutHolds.h completes_without_holds "completes but has no SENCHA_LEASE_HOLDS"
expect_rejected BadHoldsWithoutCompletes.h holds_without_completes "names a lease that does not complete"
expect_rejected BadLeaseResult.h lease_result "a lease answers no result"
expect_rejected BadResultWithoutKey.h result_without_key "so it names its result with SENCHA_RESULT"
expect_rejected BadResultKeyWithoutResult.h result_key_without_result "names a result with SENCHA_RESULT but returns no VerbResult"
expect_rejected BadAssetParameter.h asset_parameter "SENCHA_ASSET applies to an AssetRef parameter"
expect_rejected BadDataAssetParameter.h data_asset_parameter "SENCHA_DATA_ASSET applies to an AuthoredDataRef parameter"
expect_rejected BadRangeAndMin.h range_and_min "has both SENCHA_RANGE and SENCHA_MIN or SENCHA_MAX"
expect_rejected BadCosmeticQuery.h cosmetic_query "describe a verb or a lease"
expect_rejected BadCosmeticPrediction.h cosmetic_prediction "a cosmetic verb is predicted as presentation"
expect_rejected BadReleaseArguments.h release_arguments "takes only the invocation"
expect_rejected BadCompletesOnVerb.h completes_on_verb "only a lease is held long enough to complete"
expect_rejected BadEmptyRecord.h empty_record "has no SENCHA_FIELD"

# Generated, then refused by the compiler: a target and an event source that
# name a type which exists but is not a component.
if [[ -n "${CXX}" ]]; then
    if ! run BadNonComponentTarget.h non_component; then
        echo "FAIL: non_component: the generator refused what the compiler should refuse:"
        sed 's/^/    /' "${WORK}/non_component.log"
        status=1
    else
        printf '#include "%s/BadNonComponentTarget.h"\n#include "%s/non_component.h"\n' \
            "${FIXTURES}" "${WORK}" > "${WORK}/non_component.cpp"
        if "${CXX}" -std=c++20 -fsyntax-only -I"${ROOT}/engine/include" -I"${GENERATED}" \
               "${WORK}/non_component.cpp" >"${WORK}/non_component.compile.log" 2>&1; then
            echo "FAIL: non_component: a target naming a non-component compiled"
            status=1
        elif ! grep -q "SENCHA_TARGET(BadNotAComponent) on parameter 'thing' names a type that is not a component" \
                 "${WORK}/non_component.compile.log" \
             || ! grep -q "SENCHA_EVENT_SOURCE(BadNotAComponent) names a type that is not a component" \
                 "${WORK}/non_component.compile.log"; then
            echo "FAIL: non_component: refused without naming the declaration"
            sed 's/^/    /' "${WORK}/non_component.compile.log" | head -40
            status=1
        fi
    fi
fi

if [[ -n "${CMAKE}" ]]; then
    PROBE="${WORK}/depfile"
    mkdir -p "${PROBE}/src"
    printf '#pragma once\ninline constexpr int kDepfileCount = 3;\n' > "${PROBE}/src/DepfileConstants.h"
    cat > "${PROBE}/src/DepfileProvider.h" <<'HEADER'
#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>

#include "DepfileConstants.h"

class DepfileProvider
{
public:
    SENCHA_VERB("test.depfile.count")
    VerbAdmission Count(SENCHA_ARG("count") int count = kDepfileCount);
};
HEADER
    : > "${PROBE}/src/probe.cpp"
    cat > "${PROBE}/CMakeLists.txt" <<LISTS
cmake_minimum_required(VERSION 3.20)
project(SenchaDepfileProbe LANGUAGES CXX)
add_executable(sencha::component-codegen IMPORTED)
set_target_properties(sencha::component-codegen PROPERTIES IMPORTED_LOCATION "${TOOL}")
include("${ROOT}/cmake/SenchaComponentCodegen.cmake")
add_library(depfile_probe STATIC src/probe.cpp)
target_include_directories(depfile_probe PRIVATE "${ROOT}/engine/include" "${GENERATED}" "\${CMAKE_CURRENT_SOURCE_DIR}/src")
sencha_generate_component_metadata(depfile_probe INCLUDE_ROOT "\${CMAKE_CURRENT_SOURCE_DIR}/src"
                                   API_HEADERS src/DepfileProvider.h)
LISTS
    COMPANION="${PROBE}/build/generated-components/depfile_probe/DepfileProvider.sencha.h"
    generator_arguments=()
    [[ -n "${CMAKE_GENERATOR_NAME}" ]] && generator_arguments+=(-G "${CMAKE_GENERATOR_NAME}")
    [[ -n "${MAKE_PROGRAM}" ]] && generator_arguments+=("-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
    if ! "${CMAKE}" -S "${PROBE}" -B "${PROBE}/build" "${generator_arguments[@]}" >"${WORK}/depfile.log" 2>&1 \
       || ! "${CMAKE}" --build "${PROBE}/build" --target depfile_probe_component_index >>"${WORK}/depfile.log" 2>&1; then
        echo "FAIL: depfile: the probe project did not configure and generate:"
        sed 's/^/    /' "${WORK}/depfile.log" | tail -30
        status=1
    elif ! grep -q 'static_cast<int>(3)' "${COMPANION}"; then
        echo "FAIL: depfile: the companion does not hold the included header's default"
        status=1
    else
        printf '#pragma once\ninline constexpr int kDepfileCount = 4;\n' > "${PROBE}/src/DepfileConstants.h"
        touch -d '+2 seconds' "${PROBE}/src/DepfileConstants.h"
        "${CMAKE}" --build "${PROBE}/build" --target depfile_probe_component_index >>"${WORK}/depfile.log" 2>&1
        if ! grep -q 'static_cast<int>(4)' "${COMPANION}"; then
            echo "FAIL: depfile: changing a header the provider includes did not regenerate its companion"
            status=1
        fi
    fi
fi

reported="$("${TOOL}" --format-version)"
declared="$(sed -n 's/.*kComponentCodegenFormatVersion = \([0-9]*\);.*/\1/p' \
    "${ROOT}/engine/include/core/metadata/ComponentDefinition.h")"
if [[ "${reported}" != "${declared}" ]]; then
    echo "FAIL: --format-version reports ${reported}; ComponentDefinition.h reads ${declared}"
    status=1
fi

if [[ ${status} -eq 0 ]]; then
    echo "component codegen: OK"
fi
exit ${status}
