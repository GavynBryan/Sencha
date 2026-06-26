cmake_minimum_required(VERSION 3.20)

# Out-of-source builds only. Configuring in the source tree scatters CMakeCache,
# CMakeFiles/, generated shaders, etc. across the repo -- the exact mess this
# project's build hygiene is meant to avoid. Use: cmake -S . -B build
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are forbidden. Run from the repo root:\n"
        "  cmake -S . -B build\n"
        "  cmake --build build\n"
        "(or use a preset: cmake --preset dev). Remove the stray CMakeCache.txt "
        "and CMakeFiles/ this attempt just created before retrying.")
endif()

# VERSION is the engine SDK version: the semantic version of the published
# binary product (engine + editor + app + headers), distinct from the per-build
# ABI fingerprint in engine/CMakeLists.txt. The fingerprint catches accidental
# binary skew between a host and a module; this version is the human-meaningful
# release a game project targets via find_package(Sencha <version>).
project(Sencha VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The engine is a shared library (game modules and tools must share ONE copy of
# its globals/registries — see docs/plans/sencha-level-editor/02-...md §2.1).
# Position-independent code everywhere so the static third-party deps (bc7enc,
# imgui, …) link cleanly into the shared engine, and game modules stay PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Emit compile_commands.json for clangd and other tooling. It lands in the
# build dir; symlink the chosen one to the repo root if your editor needs it.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

enable_testing()

# Standard bin/lib/include destinations for the install/export of the SDK
# (real-engine-roadmap.md Phase 0). Included at the root so every subdirectory's
# install rules and the engine's INSTALL_INTERFACE include dir agree on layout.
include(GNUInstallDirs)

# All SENCHA_ENABLE_* feature flags are declared here, before any subdirectory
# consumes them.
include("${CMAKE_SOURCE_DIR}/cmake/SenchaOptions.cmake")

if(SENCHA_ENABLE_TSAN)
    if(MSVC)
        message(FATAL_ERROR "SENCHA_ENABLE_TSAN requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()

# Static link the C++ runtime so executables don't depend on MinGW DLLs at runtime.
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

add_subdirectory(engine)

add_subdirectory(app)
add_subdirectory(editor)
add_subdirectory(example)
add_subdirectory(test)
