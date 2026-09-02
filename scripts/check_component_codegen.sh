#!/usr/bin/env bash
# The generator's own contract: what it emits for the golden component, that it
# emits the same bytes twice, that a malformed declaration is refused without
# leaving output behind -- an empty companion would read as a component that no
# longer exists -- and that the format it reports is the one the headers read.
#
#   check_component_codegen.sh <tool> <source-root> <generated-public-dir>
set -uo pipefail

TOOL="${1:?tool path required}"
ROOT="${2:?source root required}"
GENERATED="${3:?generated-public dir required}"

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

run GoldenComponent.h golden_again
if ! diff -q "${WORK}/golden.h" "${WORK}/golden_again.h" >/dev/null \
   || ! diff -q "${WORK}/golden.index" "${WORK}/golden_again.index" >/dev/null; then
    echo "FAIL: two runs produced different output"
    status=1
fi

expect_rejected BadPredictedNotReplicated.h predicted "predicted but not replicated"
expect_rejected BadNoIdentity.h no_identity "no SENCHA_COMPONENT identity"
expect_rejected BadShortChunk.h short_chunk "exactly four characters"

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
