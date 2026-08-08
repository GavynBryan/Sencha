# Sencha build options.
#
# All SENCHA_ENABLE_* feature flags are declared here in one place so the set of
# build configurations is discoverable without grepping every CMakeLists. The
# options must be declared before any subdirectory consumes them, so the root
# CMakeLists includes this immediately after project().
#
# The *effects* of an option live where they apply: source selection in
# engine/CMakeLists.txt, sanitizer flags in the root CMakeLists. This file only
# declares the switches and enforces cross-option invariants.
include_guard(GLOBAL)

option(SENCHA_ENABLE_VULKAN
    "Build Sencha Vulkan graphics foundation"
    ON)

option(SENCHA_ENABLE_HOT_RELOAD
    "Link glslang into the engine for runtime GLSL compilation and live shader reload. Always OFF in release builds -- shipping binaries must not contain a GLSL compiler."
    OFF)

option(SENCHA_ENABLE_DEBUG_UI
    "Build the ImGui-based runtime debug overlay (console + timing panels, grave-key toggle). ON by default: game builds ship it so the runtime is tunable in the field; a host opts out per-process via EngineConfig.Console.UiEnabled. Requires SENCHA_ENABLE_VULKAN."
    ON)

option(SENCHA_ENABLE_COOK
    "Build the dev-only asset cook layer (import-on-demand, cooked cache, importers). Always OFF in shipping builds -- cooked data ships, importers do not."
    ON)

option(SENCHA_ENABLE_RENDER_PROFILING
    "Build the renderer instrumentation ladder (render.profile.mode counters/gpu/capture: GPU timestamp pools, debug labels, capture export, stats panel). OFF removes those bodies entirely; the pass-local counter accumulation and RenderStats stay compiled as a test seam. OFF in the shipping preset."
    ON)

option(SENCHA_ENABLE_TSAN
    "Build with ThreadSanitizer (GCC/Clang only). Used to run the test suites against the job system's concurrent core; see docs/ecs/parallelization.md."
    OFF)

option(SENCHA_ENABLE_ASAN
    "Build with AddressSanitizer (GCC/Clang only). Catches use-after-free, double-free, and heap corruption with symbolized allocation/free stacks. Mutually exclusive with SENCHA_ENABLE_TSAN."
    OFF)

option(SENCHA_BUILD_TEMPLATE
    "Build the template game module in-tree against the engine being built. Engine-dev convenience: the module rebuilds with the engine in one build, so the host/module ABI fingerprint can never skew (no SDK install/rebuild dance). Writes template/build/game.so; the standalone SDK build of template/ is unaffected."
    ON)

option(SENCHA_WARNINGS_AS_ERRORS
    "Treat first-party compiler warnings as errors. ON in the dev preset (and everything inheriting it: tsan, ci) so a new warning fails the build where it is introduced. OFF by default so bare configures, an installed SDK, and toolchains whose diagnostics we have not triaged still build."
    OFF)

set(SENCHA_GAME_PROJECT_DIR "" CACHE PATH
    "Optional external game project to build against the in-tree engine. The project must provide a CMakeLists.txt that supports being added as a subdirectory.")

# Cross-option invariants.
# The overlay draws through the Vulkan backend, so a no-Vulkan build simply has
# nowhere to put it. Forcing it off beats failing the configure: the debug UI is
# on by default everywhere, and a headless build asking for no graphics should
# not have to also know to switch off a UI it could never have shown.
if(SENCHA_ENABLE_DEBUG_UI AND NOT SENCHA_ENABLE_VULKAN)
    message(STATUS "SENCHA_ENABLE_DEBUG_UI forced OFF: it requires SENCHA_ENABLE_VULKAN=ON")
    set(SENCHA_ENABLE_DEBUG_UI OFF CACHE BOOL "" FORCE)
endif()
