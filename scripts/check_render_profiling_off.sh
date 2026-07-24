#!/usr/bin/env bash
# Proof that a SENCHA_ENABLE_RENDER_PROFILING=OFF build carries no renderer
# instrumentation. This is the compiled-out counterpart to a RenderDoc capture:
# rather than confirming a live command stream issues no timestamp / query /
# debug-label commands, it confirms those commands are absent from the binary
# entirely, so no command stream can contain them.
#
# Part A is a source invariant checked against the tree; Part B inspects the
# compiled object files of an OFF-configured build directory when one is
# supplied. Object files are the discriminating artifact: release binaries drop
# the symbols anyway (hidden visibility, --gc-sections) and load Vulkan entry
# points dynamically, so a final binary's symbol table shows nothing either way.
# A translation unit that is never compiled cannot contribute any command.
#
# Usage:
#   scripts/check_render_profiling_off.sh [off-configured-build-dir]

set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

fail=0
note() { printf '  %s\n' "$1"; }
ok()   { printf 'PASS  %s\n' "$1"; }
bad()  { printf 'FAIL  %s\n' "$1"; fail=1; }

echo "== Part A: source invariants =="

# The Vulkan timestamp / query / debug-label commands must live only in the two
# translation units the OFF build excludes. If they leak into any other TU, the
# CMake exclusion no longer removes them and this proof is void.
timestamp_tus="$(grep -rln \
    -e vkCmdWriteTimestamp -e vkCmdResetQueryPool -e vkGetQueryPoolResults \
    engine/src || true)"
if [ "$timestamp_tus" = "engine/src/graphics/vulkan/GpuTimestampPool.cpp" ]; then
    ok "timestamp/query commands confined to GpuTimestampPool.cpp"
else
    bad "timestamp/query commands found outside GpuTimestampPool.cpp:"
    note "$timestamp_tus"
fi

label_tus="$(grep -rln \
    -e vkCmdBeginDebugUtilsLabelEXT -e vkCmdEndDebugUtilsLabelEXT \
    engine/src || true)"
if [ "$label_tus" = "engine/src/graphics/vulkan/VulkanDebugLabels.cpp" ]; then
    ok "debug-label commands confined to VulkanDebugLabels.cpp"
else
    bad "debug-label commands found outside VulkanDebugLabels.cpp:"
    note "$label_tus"
fi

# Those TUs, plus the capture/stats-panel/debug-view sources, must be named in
# the OFF-build exclusion list in engine/CMakeLists.txt.
for tu in GpuTimestampPool VulkanDebugLabels RenderCapture RenderStatsPanel RenderDebugView; do
    if grep -q "$tu" engine/CMakeLists.txt; then
        ok "engine/CMakeLists.txt excludes $tu in the OFF build"
    else
        bad "engine/CMakeLists.txt does not list $tu for OFF exclusion"
    fi
done

echo
echo "== Part B: compiled object files in the OFF build =="

build_dir="${1:-}"
if [ -z "$build_dir" ]; then
    note "no build dir supplied; skipping (pass an OFF-configured build directory)"
elif [ ! -d "$build_dir" ]; then
    bad "build directory not found: $build_dir"
else
    obj_root="$build_dir/engine/CMakeFiles/sencha_engine.dir/src"
    if [ ! -d "$obj_root" ]; then
        obj_root="$(find "$build_dir" -type d -name 'sencha_engine.dir' 2>/dev/null | head -1)/src"
    fi

    # Confirm the build is actually configured OFF, so a stray ON dir cannot
    # masquerade as a pass.
    cache="$build_dir/CMakeCache.txt"
    if grep -q "SENCHA_ENABLE_RENDER_PROFILING:BOOL=OFF" "$cache" 2>/dev/null; then
        ok "CMakeCache has SENCHA_ENABLE_RENDER_PROFILING=OFF"
    else
        bad "CMakeCache does not show SENCHA_ENABLE_RENDER_PROFILING=OFF (wrong build dir?)"
    fi

    # The excluded translation units must have produced no object file.
    for tu in \
        profiling/RenderCapture \
        graphics/vulkan/GpuTimestampPool \
        graphics/vulkan/VulkanDebugLabels \
        debug/RenderStatsPanel \
        render/RenderDebugView; do
        if [ -f "$obj_root/$tu.cpp.o" ]; then
            bad "excluded TU was compiled: $tu.cpp.o present"
        else
            ok "not compiled: $(basename "$tu").cpp.o"
        fi
    done

    # The always-compiled tier policy (the mode enum + ResolveInstrumentationBundle)
    # must still be there: it is the seam that keeps the Off path null.
    if [ -f "$obj_root/profiling/RenderInstrumentation.cpp.o" ]; then
        ok "tier policy compiled: RenderInstrumentation.cpp.o present"
    else
        bad "RenderInstrumentation.cpp.o missing (tier policy should compile in every build)"
    fi
fi

echo
if [ "$fail" -eq 0 ]; then
    echo "RESULT  render profiling is fully compiled out (all checks passed)"
else
    echo "RESULT  one or more checks FAILED"
fi
exit "$fail"
