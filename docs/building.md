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
- **Clang development libraries** — `clang-devel llvm-devel` (Fedora) or
  `clang libclang-dev llvm-dev` (Debian). They build
  `sencha-component-codegen`, the host tool that generates component metadata
  from the annotated headers. Only building the engine needs them; a game
  module built against an installed SDK runs the shipped binary.

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
| `SENCHA_COMPONENT_CODEGEN_EXECUTABLE` | empty | A prebuilt `sencha-component-codegen` to run instead of building the tool. See [The component generator on MSVC](#the-component-generator-on-msvc). |

Override any of them on a classic configure with `-DSENCHA_ENABLE_FOO=ON/OFF`.

## The component generator on MSVC

`sencha-component-codegen` links Clang's libraries, which no MSVC toolchain
carries. Build the tool as its own project against an LLVM release archive
(`clang+llvm-<version>-x86_64-pc-windows-msvc`), then hand the binary to the
engine configure:

```sh
cmake -S tools/component-codegen -B build-codegen -DClang_DIR=C:/llvm/lib/cmake/clang
cmake --build build-codegen --config Release
cmake --install build-codegen --config Release --prefix C:/sencha-codegen
cmake -B build -DSENCHA_COMPONENT_CODEGEN_EXECUTABLE=C:/sencha-codegen/bin/sencha-component-codegen.exe ...
```

The tool is built in Release because the archive's libraries link the release
CRT; the engine configuration is independent of it. The Windows CI job in
`.github/workflows/ci.yml` is the worked example.

A prebuilt generator must come from the same revision as the headers: the
configure asks it for `--format-version` and refuses one whose companion format
is not the one `core/metadata/ComponentDefinition.h` reads. An installed SDK
makes the same check of its shipped generator from `find_package(Sencha)`.

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

## Headless

Two different things share the word, and only one of them works.

**Running headless — works.** `app --headless` builds no window and no graphics
services, and runs the frame loop anyway: async commits drain, zone residency
resolves, the tick scheduler paces fixed ticks, and the schedule's simulation
phases run. The render phases are not registered at all. This is the
dedicated-host shape and the CI simulation-soak shape.

```sh
app --headless --game path/to/game.so +map levels/<name>
```

A headless host runs until something asks it to stop. `Ctrl-C` and `SIGTERM`
both shut it down through the ordinary exit path, so the world tears down and
connected peers are told rather than the process vanishing; so does typing
`quit` at its terminal, `Engine::RequestExit()`, and
`+set app.exit_after_frames <n>`. There is no window to close, so a host with
none of those runs forever — which is correct for a server.

Its terminal is its console. Anything typed there runs as a console command
(`net.max_peers 8`, `quit`), and the startup script's output is logged, so which
map loaded and which port an ephemeral bind actually took are visible without a
window. A host started with its input closed simply has no console; it is not an
error.

The pacing default is twice the fixed tick rate, derived from
`time.fixed_tick_rate` rather than written down separately: a frame that presents
nothing is still the frame that pumps the network, and nothing else paces a loop
with no vsync. `+set r.target_fps <n>` overrides it.

The game module has to cooperate, and the bundled `template/` now does: its
asset stack composes without graphics services, holding everything except the
caches that own GPU resources. A scene that references meshes or textures still
loads there — an asset of a kind this process cannot hold is declined rather
than failing the load, so a host gets the entities and collision it simulates
with and no bodies to draw. A module of your own needs the same treatment:
reach for `Engine::TryGraphics()` rather than `Engine::Graphics()`.

Nobody plays in a dedicated host. That is a separate fact from having no
graphics (`EngineRuntimeConfig::HasLocalPlayer`, set by `--headless`), because
the two come apart: a scripted client is headless *with* a player. The authority
simulates every peer's pawn; it just never provisions one of its own.

### Packaging a server and a client

```sh
scripts/package_bundle.sh --content ~/MyProject --map levels/EntranceHall --out /tmp/bundles
```

Writes two self-contained directories — a dedicated server and a client that
joins one — carrying the host binary, the game module, cooked content, and the
non-system shared libraries a clean machine will not have. Both ends must be
built from the same source: a mismatch is refused at the handshake.

### From the editor

`playserver [map]` in Kyusu launches the same pair: a headless authority and a
windowed client joined to it. `editor.pie.host_port` picks the port. This is the
topology a shipped session runs, and unlike a listen server it does not make the
host's rendering compete with the client's.

**Building without Vulkan — does not work.** `SENCHA_ENABLE_VULKAN=OFF`
fails to compile because the render layer (`engine/src/render/…`,
`include/render/static_mesh/GpuStaticMesh.h`) includes Vulkan/VMA headers
unconditionally even though those files sit outside `graphics/vulkan/`. Building
or testing without a GPU is fine — the test suite is non-graphical — but the
**Vulkan SDK headers must be present**. Decoupling the render layer is tracked
under "Future work" in
[cmake-build-hygiene-plan.md](cmake-build-hygiene-plan.md). Note that the two are
independent: running headless does not need a no-Vulkan build, it just declines
to initialize the graphics services a normal build contains.

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
