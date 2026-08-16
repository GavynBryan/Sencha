#!/usr/bin/env bash
# Runs clang-tidy over the render, graphics, profiling, and asset translation
# units using the repository's own compile database, so the analyzer sees the
# same flags, feature defines, and Vulkan include paths the build does.
#
# Unlike the other scripts/check_*.sh guards, this one cannot run before the
# build: clang-tidy needs compile_commands.json, and it parses generated
# headers (embedded SPIR-V, the ABI fingerprint) that only exist once the build
# has produced them.
#
# Advisory. It reports what the checks in .clang-tidy find and returns their
# status, but nothing gates on it yet: this tree has no continuously maintained
# baseline, so a first run is a report to read rather than a pass/fail.
#
# Usage:
#   scripts/run_render_tidy.sh [build-dir]        # default: build

set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

build_dir="${1:-build}"
compile_db="$build_dir/compile_commands.json"

for tool in clang-tidy run-clang-tidy; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "FAIL  $tool not found on PATH"
        echo "      Install the clang-tidy package (it ships run-clang-tidy)."
        exit 2
    fi
done

if [ ! -f "$compile_db" ]; then
    echo "FAIL  no compile database at $compile_db"
    echo "      Configure and BUILD first; generated headers must exist:"
    echo "        cmake --preset dev && cmake --build --preset dev --parallel"
    exit 2
fi

# Findings are only comparable against a recorded baseline if the tool version
# is recorded with them.
echo "== clang-tidy =="
clang-tidy --version | sed 's/^/  /'
echo "  compile database: $compile_db"
echo "  checks: see .clang-tidy"
echo

# The four engine subtrees .clang-tidy's HeaderFilterRegex covers. Anchored on
# the source root so a path like engine/src/render never matches a build
# directory copy.
tu_filter="$repo_root/engine/src/(assets|render|graphics|profiling)/"

echo "== analyzing $tu_filter =="
report="$(mktemp)"
trap 'rm -f "$report"' EXIT
run-clang-tidy -p "$build_dir" -quiet "$tu_filter" 2>&1 | tee "$report"
status="${PIPESTATUS[0]}"

# WarningsAsErrors is empty, so run-clang-tidy exits 0 whether or not it found
# anything. Count the distinct diagnostics instead, and report per check so a
# baseline can be compared without diffing whole logs.
findings="$(grep -cE '^[^ ].*: warning: ' "$report")"

echo
echo "== findings by check =="
grep -oE '\[(bugprone|performance)-[a-z0-9-]+\]' "$report" | sort | uniq -c | sort -rn | sed 's/^/  /'
echo
echo "RESULT  $findings finding(s); run-clang-tidy exit $status (advisory)"
exit "$status"
