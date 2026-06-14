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
| `dev`     | Debug      | on     | on   | off      | Daily development. Builds in `build/`. |
| `dev-ui`  | Debug      | on     | on   | **on**   | Adds the ImGui debug overlay. `build-dev-ui/`. |
| `release` | Release    | on     | on   | off      | Optimized. Hot-reload stays off — shipping binaries carry no GLSL compiler. |
| `tsan`    | Debug      | on     | on   | off      | ThreadSanitizer for the job system. GCC/Clang only. `build-tsan/`. |
| `ci`      | Debug      | on     | on   | off      | For CI runners with the Vulkan SDK; no GPU needed to build/run the (non-graphical) tests. `build-ci/`. |

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

Override any of them on a classic configure with `-DSENCHA_ENABLE_FOO=ON/OFF`.

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
