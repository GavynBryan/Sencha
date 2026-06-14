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
    "Build the ImGui-based debug overlay frontend (ConsolePanel, ImGuiDebugOverlay). Requires SENCHA_ENABLE_VULKAN."
    OFF)

option(SENCHA_ENABLE_COOK
    "Build the dev-only asset cook layer (import-on-demand, cooked cache, importers). Always OFF in shipping builds -- cooked data ships, importers do not."
    ON)

option(SENCHA_ENABLE_TSAN
    "Build with ThreadSanitizer (GCC/Clang only). Used to run the test suites against the job system's concurrent core; see docs/ecs/parallelization.md."
    OFF)

# Cross-option invariants.
if(SENCHA_ENABLE_DEBUG_UI AND NOT SENCHA_ENABLE_VULKAN)
    message(FATAL_ERROR "SENCHA_ENABLE_DEBUG_UI requires SENCHA_ENABLE_VULKAN=ON")
endif()
