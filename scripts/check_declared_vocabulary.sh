#!/usr/bin/env bash
#
# The engine declares its verbs and queries one way: annotated methods read by
# sencha-component-codegen (docs/gameplay/authored-api.md). A VerbDefinition or
# AuthoredQueryDefinition built by hand under engine/src outside authored/, which
# owns the catalogs and the generated form's helpers, is a second declaration
# form. Developer console commands are not in scope.
#
# Usage: check_declared_vocabulary.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
pattern='(^|[^A-Za-z_&*:<])(VerbDefinition|AuthoredQueryDefinition)[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*([;={(]|$)'

found="$(grep -rnE "$pattern" "$ROOT/engine/src" --include='*.cpp' --include='*.h' \
    | grep -v "^$ROOT/engine/src/authored/" \
    | grep -vE '^[^:]+:[0-9]+:[[:space:]]*//')"

if [[ -n "$found" ]]; then
    echo "FAIL: a verb or query is declared by hand outside engine/src/authored:"
    echo "$found" | sed 's/^/    /'
    exit 1
fi
echo "PASS: every engine verb and query is declared by its method"
