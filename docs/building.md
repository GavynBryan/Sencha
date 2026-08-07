# Building Sencha

The canonical build flow uses **CMake presets** (`CMakePresets.json` at the repo
root). All builds are out-of-source; configuring inside the source tree is
blocked by the root `CMakeLists.txt`.

## Prerequisites

- **CMake 3.23+** (for presets; 3.20+ works via the fallback flow below)
- **Ninja** (the generator all presets use)
- A **C++20** compiler (GCC, Clang, or MSVC)
- **SDL3** — discovered via `find_package(SDL3 REQUIRED CONFIG)`
- **Vulkan SDK** — currently required (see [Headless builds](#headless-builds))

FetchContent pulls the rest (stb, GoogleTest, VMA, ImGui, glslang, and the cook
encoders) automatically at configure time, gated on the relevant feature flag.

## Quick start

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
```

This produces a full Vulkan + cook debug build in `build/` and runs the test
suite.

## Presets

| Preset    | Build type | Vulkan | Cook | Debug UI | Notes |
|-----------|------------|--------|------|----------|-------|
| `dev`     | Debug      | on     | on   | on       | Daily development. Builds in `build/`. |
| `release` | Release    | on     | on   | on       | Optimized. Hot-reload stays off — shipping binaries carry no GLSL compiler. |
| `tsan`    | Debug      | on     | on   | on       | ThreadSanitizer for the job system. GCC/Clang only. `build-tsan/`. |
| `ci`      | Debug      | on     | on   | on       | For CI runners with the Vulkan SDK; no GPU needed to build/run the (non-graphical) tests. `build-ci/`. |

The debug UI ships in every build, including `release`: the console is a
player-facing feature, not a development-only one. A host that does not want it
sets `EngineConfig.Console.UiEnabled = false` per process (the editors do). A
no-Vulkan build has nowhere to draw it and forces it off automatically.

Each configure preset has a matching build preset; `dev`, `tsan`, and `ci` also
have test presets (`ctest --preset <name>`). List them with:

```sh
cmake --list-presets
```

## Feature flags

All `SENCHA_ENABLE_*` options are declared in
[`cmake/SenchaOptions.cmake`](../cmake/SenchaOptions.cmake):

| Option                     | Default | Effect |
|----------------------------|---------|--------|
| `SENCHA_ENABLE_VULKAN`     | ON      | Vulkan graphics foundation (VMA). |
| `SENCHA_ENABLE_COOK`       | ON      | Dev-only asset cook layer + importers. Never ships. |
| `SENCHA_ENABLE_DEBUG_UI`   | OFF     | ImGui debug overlay. Requires `SENCHA_ENABLE_VULKAN`. |
| `SENCHA_ENABLE_HOT_RELOAD` | OFF     | glslang for live GLSL reload. Never in release. |
| `SENCHA_ENABLE_TSAN`       | OFF     | ThreadSanitizer (GCC/Clang). |
| `SENCHA_WARNINGS_AS_ERRORS` | OFF (ON in `dev`) | Compiler warnings fail the build. See [Warnings](#warnings). |

Override any of them on a classic configure with `-DSENCHA_ENABLE_FOO=ON/OFF`.

## Warnings

Every first-party target calls `sencha_warnings()` from
[`cmake/SenchaWarnings.cmake`](../cmake/SenchaWarnings.cmake), which is the one
place the warning set is defined: `-Wall -Wextra -Wshadow -Wnon-virtual-dtor`
on GCC and Clang, `/W4 /permissive-` on MSVC. Third-party targets never call it,
and the tree builds clean.

`SENCHA_WARNINGS_AS_ERRORS` adds `-Werror` (`/WX`). The `dev` preset sets it ON,
so `tsan` and `ci` inherit it and a new warning fails the build where
it is introduced. To get past one mid-change:

```sh
cmake --preset dev -DSENCHA_WARNINGS_AS_ERRORS=OFF
```

`release` and `profile` deliberately leave it OFF. Warnings from optimization
passes vary with the compiler version, and a toolchain upgrade must not be able
to block a shipping build.

Fix warnings at the cause; do not add `-Wno-*`. A third-party implementation body
included into a first-party translation unit is the one exception, and it is
guarded at the include site — see
`engine/src/graphics/vulkan/VulkanMemoryAllocatorImpl.cpp`.

## Joint engine and game development

An external game project can join the engine source build without moving into
the Sencha repository or installing a new SDK after each engine change:

```sh
cmake --preset dev -DSENCHA_GAME_PROJECT_DIR=../my-game
cmake --build build --target my_game_module --parallel
```

The external project must support `add_subdirectory()` and build its module
with `sencha_game_module()`. It links the in-tree `sencha::engine`, so the host
and module always receive the same ABI fingerprint. Leaving
`SENCHA_GAME_PROJECT_DIR` empty preserves the normal engine-only build.

## Without presets (CMake < 3.23)

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Headless builds

There is **no working no-Vulkan configuration yet**. `SENCHA_ENABLE_VULKAN=OFF`
fails to compile because the render layer (`engine/src/render/…`,
`include/render/static_mesh/GpuStaticMesh.h`) includes Vulkan/VMA headers
unconditionally even though those files sit outside `graphics/vulkan/`. Building
or testing without a GPU is fine — the test suite is non-graphical — but the
**Vulkan SDK headers must be present**. Decoupling the render layer is tracked
under "Future work" in
[cmake-build-hygiene-plan.md](cmake-build-hygiene-plan.md).

## Cleaning a dirty tree

Build output is ignored by `.gitignore`, but if a tree has accumulated junk:

```sh
git clean -ndX     # dry run — review what would be deleted
git clean -fdX     # delete all ignored files (NOT just build dirs)
```

Or just remove the build trees:

```sh
rm -rf build build-* out install
```

To recover from an accidental in-source configure (the root CMakeLists blocks
it, but a stray `CMakeCache.txt`/`CMakeFiles/` can be left behind):

```sh
rm -rf CMakeCache.txt CMakeFiles
```

## compile_commands.json

Every configure writes `compile_commands.json` into its build dir. Point clangd
at it, or symlink the one you use to the repo root:

```sh
ln -sf build/compile_commands.json compile_commands.json
```
