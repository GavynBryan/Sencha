#!/usr/bin/env bash
# Checks the render layer for the platform assumptions that would break a
# Windows build. The renderer reaches the OS only through SDL, VMA, and the
# Vulkan loader, which is what makes it portable; each check below guards one
# way that property gets lost. Failures are portability regressions, not style.
#
# Scope is the render layer only: engine/{src,include}/{render,graphics,profiling}.
# The window/platform layer is deliberately excluded -- it is where the SDL
# implementation lives, and app/, assets/, and the editors are out of scope for
# the renderer branch.
#
# Usage: check_render_portability.sh [repo-root]
set -uo pipefail

root=$(realpath "${1:-$(dirname "$0")/..}")
cd "$root"

dirs=(
    engine/src/render engine/include/render
    engine/src/graphics engine/include/graphics
    engine/src/profiling engine/include/profiling
)

present=()
for d in "${dirs[@]}"; do
    [ -d "$d" ] && present+=("$d")
done
if [ ${#present[@]} -eq 0 ]; then
    echo "FAIL: no render layer directories found under $root" >&2
    exit 1
fi

failures=0

# Drops line comments and whole-line block comment bodies from grep output, so
# prose that happens to contain a checked word is not a finding. Input and
# output are grep's "path:line:text" form.
strip_comments() {
    sed -E 's@//.*$@@' | grep -vE '^[^:]+:[0-9]+:[[:space:]]*\*' || true
}

# $1 check name, $2 why it matters, $3 offending lines (empty means pass)
check() {
    local name=$1 why=$2 hits=$3
    if [ -n "$hits" ]; then
        echo "FAIL: $name"
        echo "  $why"
        echo "    ${hits//$'\n'/$'\n'    }"
        failures=$((failures + 1))
    else
        echo "ok:   $name"
    fi
}

# POSIX-only headers. Anything here has no Windows equivalent and would need a
# platform split in a layer that currently has none.
hits=$(grep -rnE '#include[[:space:]]*<(unistd|dlfcn|pthread|sys/[a-z]+)\.h>' "${present[@]}" | strip_comments)
check "no POSIX-only headers" \
      "The render layer reaches the OS through SDL/VMA/Vulkan only." "$hits"

# Platform conditionals. The render layer has none today; the first one is
# where per-platform behavior starts diverging silently.
hits=$(grep -rnE '#[[:space:]]*(if|ifdef|ifndef|elif).*(_WIN32|__linux__|__APPLE__|__unix__)' "${present[@]}" | strip_comments)
check "no platform conditionals" \
      "Platform branches belong in the platform layer, not the renderer." "$hits"

# `long` is 32-bit on Windows (LLP64) and 64-bit on Linux (LP64). Requiring a
# declarator after the type keeps prose and `long long` casts out of the match.
hits=$(grep -rnE '(^|[^_[:alnum:]])(unsigned[[:space:]]+)?long[[:space:]]+[A-Za-z_][A-Za-z_0-9]*[[:space:]]*(;|=|,|\)|\[)' "${present[@]}" | strip_comments)
check "no bare long" \
      "long is 32-bit on Windows and 64-bit on Linux; use fixed-width types." "$hits"

# Direct WSI surface calls. SDL picks the platform surface extension; naming
# one here hardcodes the windowing system.
hits=$(grep -rnE 'VK_KHR_(XCB|XLIB|WAYLAND|WIN32)_SURFACE|vkCreate(Xcb|Xlib|Wayland|Win32)SurfaceKHR' "${present[@]}" | strip_comments)
check "no direct platform surface calls" \
      "Surface creation goes through SDL_Vulkan_CreateSurface." "$hits"

# Raw threading. The engine has two concurrency lanes and the render path is
# single-threaded; a raw thread here is both a portability and a design break.
hits=$(grep -rnE '#include[[:space:]]*<(thread|future)>|std::(thread|async)[[:space:]]*[(<]' "${present[@]}" | strip_comments)
check "no raw threads in the render layer" \
      "Use the job system or the async task queue." "$hits"

# A GCC diagnostic pragma with no MSVC counterpart in the same file means
# upstream warnings return under /W4 in a file written to suppress them.
hits=""
while IFS= read -r f; do
    [ -n "$f" ] || continue
    if ! grep -qE '#pragma warning[[:space:]]*\(' "$f"; then
        hits="${hits}${f}: GCC diagnostic pragma without an MSVC counterpart"$'\n'
    fi
done < <(grep -rlE '#pragma GCC diagnostic' "${present[@]}" 2>/dev/null || true)
check "diagnostic pragmas cover both compilers" \
      "MSVC does not read GCC pragmas; the suppression silently lapses." "$hits"

echo
if [ "$failures" -ne 0 ]; then
    echo "$failures portability check(s) failed"
    exit 1
fi
echo "render layer portability checks passed"
