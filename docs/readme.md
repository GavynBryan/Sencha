# Sencha

Sencha is a C++20 game engine built around explicit systems, explicit services, and a flat domain tree. It doesn't hide the machinery — dependencies are traced by hand, ownership is visible at construction, and backends live behind opt-in interfaces.

Sencha is best enjoyed without added sweeteners.

## Repository Layout

```
app/        Minimal application target.
docs/       Project notes.
engine/     Shared engine library: core, math, world, render, graphics, audio, input, time, runtime.
example/    Small executable examples.
test/       GoogleTest-based engine tests.
```

## Requirements

- CMake 3.20+
- C++20 compiler
- SDL3 (via `find_package(SDL3 REQUIRED CONFIG)`)
- Vulkan SDK (when `SENCHA_ENABLE_VULKAN` is on — default)

Optional: `SENCHA_ENABLE_HOT_RELOAD` (glslang), `SENCHA_ENABLE_DEBUG_UI` (ImGui).

## Build

```powershell
cmake -S . -B build
cmake --build build
```

Without Vulkan:

```powershell
cmake -S . -B build -DSENCHA_ENABLE_VULKAN=OFF
cmake --build build
```

Run tests:

```powershell
ctest --test-dir build --output-on-failure
```

## Examples

After building, run the JasmineGreen sprite example (requires Vulkan):

```powershell
.\build\example\JasmineGreen\JasmineGreen.exe
```

## Design Notes

- Services are ordinary objects owned by a host — no service locator, no global singletons.
- Systems declare dependencies through construction, not through runtime lookup.
- Backends live behind interfaces rather than leaking upward through the engine.
- Load-time formats compile into runtime tables before hot-path use.

That makes setup slightly more explicit but keeps ownership, dependency direction, and update order easy to inspect.
