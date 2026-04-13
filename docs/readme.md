# Sencha

Sencha is a C++20 game engine project built around explicit systems, explicit services, and a flat engine tree organized by domain.

It is not trying to hide the machinery. Services are registered by hand, systems receive what they need through visible construction paths, and backend implementation code stays behind opt-in interfaces. If a piece of code depends on something, the goal is that you can trace the relationship directly.

Sencha is best enjoyed without added sweeteners.

## Current Status

Sencha is early and actively changing. The repository currently contains:

- Core service and system hosting.
- Logging with console and file sinks.
- RAII lifetime handles for attach/detach ownership patterns.
- Dense batch containers for referenced and owned data.
- Event buffers.
- A lightweight JSON DOM and parser for load-time config.
- Binary serialization helpers.
- An input mapping pipeline that compiles JSON config into runtime binding tables.
- Basic math, identity, and window abstraction types.
- SDL3-backed window, video, and input ingestion services.
- Vulkan bootstrap services for instance, device, queues, surface, swapchain, and frame flow.
- GoogleTest coverage for core, math/geometry, and engine feature code.
- Small examples for input mapping and SDL/Vulkan window startup.

The engine is still more framework than finished product. APIs may move while the foundation settles.

## Architecture

Sencha is organized under one public include root and one source root:

```text
engine/
  include/
    core/
    input/
    leaves/transform/
    math/
      geometry/
    render/backend/
    window/
  src/
    core/
    input/
    leaves/transform/
    math/
      geometry/
    render/backend/
    window/
```

The old layer names are gone. Core contains the bootstrap infrastructure, math carries reusable value types and geometry, input and window own their public API plus SDL-backed implementations, render/backend contains backend-specific rendering services, and leaves/transform contains transform defaults and hierarchy propagation.

### Core

Core contains:

- `ServiceHost`, `ServiceProvider`, and `IService` for explicit service registration and lookup.
- `SystemHost` and `ISystem` for ordered system execution.
- `LoggingProvider`, `Logger`, `ConsoleLogSink`, `FileLogSink`, and `LogLevel`.
- `LifetimeHandle<T, KeyT>`, `DataBatchHandle<T>`, `RefBatchHandle<T>`, and `ILifetimeOwner` for typed RAII attach/detach lifetimes.
- `RefBatch<T>` for tracking externally-owned objects with O(1) swap-and-pop removal.
- `DataBatch<T>` for owning dense, cache-friendly vectors of data.
- `EventBuffer<T>` for frame-local event accumulation.
- `JsonParser` and `JsonValue` for load-time configuration.
- `BinaryReader`, `BinaryWriter`, and serialization helper functions.

Core is intentionally structural. It gives applications a small set of primitives, then gets out of the way.

### Math And Geometry

Math and geometry contain backend-agnostic game-facing value types such as vectors, transforms, matrices, quaternions, rays, bounds, and frustums.

### Input, Window, Render

Current integrations include:

- SDL3 video and window services.
- SDL3 input ingestion and control resolution.
- Vulkan instance, physical device, logical device, queue, surface, swapchain, and frame services.

### Leaves

Leaves are engine features that sit on top of the smaller domains. Transform hierarchy defaults and propagation live under `leaves/transform`.

## Repository Layout
```text
app/       Minimal application target.
docs/      Project notes and secondary documentation.
engine/    Flat include/src engine tree.
example/   Small executable examples.
test/      GoogleTest-based engine tests.
```

## Requirements

- CMake 3.20 or newer.
- A C++20 compiler.
- SDL3 with a CMake package available to `find_package(SDL3 REQUIRED CONFIG)`.
- Vulkan SDK when `SENCHA_ENABLE_VULKAN` is enabled.
- Git and network access on first configure so CMake can fetch GoogleTest.

Vulkan support is enabled by default. Disable it if you only want the non-Vulkan engine and input examples.

## Build

Configure and build:

```powershell
cmake -S . -B build
cmake --build build
```

Configure without Vulkan:

```powershell
cmake -S . -B build -DSENCHA_ENABLE_VULKAN=OFF
cmake --build build
```

Run tests:

```powershell
ctest --test-dir build --output-on-failure
```

## Examples

After building, the example executables are placed under the CMake build tree.

Input mapping example:

```powershell
.\build\example\SenchaInputSystem\SenchaInputSystem.exe
```

SDL/Vulkan window example, when Vulkan is enabled:

```powershell
.\build\example\SdlVulkanWindow\SdlVulkanWindow.exe
```

## Design Notes

Sencha favors visible wiring over hidden runtime behavior:

- Services are ordinary objects owned by a host.
- Systems declare dependencies through construction.
- Logging categories are tied to requesting types.
- Load-time formats compile into runtime tables before hot-path use.
- Backends live behind interfaces instead of leaking upward through the whole engine.

That makes the code a little more explicit at setup time, but it keeps ownership, dependency direction, and update order easy to inspect.
