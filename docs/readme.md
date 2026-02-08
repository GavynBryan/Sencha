# Sencha

Sencha is a game engine built on the marriage of Entity-Component-System architecture and service-oriented design. Rather than choosing one paradigm or the other, Sencha treats **systems** and **services** as first-class, composable building blocks—and it makes every connection between them visible to you.

Sencha is best enjoyed without added sweeteners.

## Design Philosophy

Sencha is built around one conviction: **you should always know what's going on under the hood.**

Every dependency is explicit. Services are registered by hand, systems declare exactly which services they need through constructor injection, and nothing is resolved behind the scenes by a hidden runtime. Even if you never write low-level engine code yourself, you are the one orchestrating it—wiring services into hosts, choosing which systems process them, and deciding the order they run in.

There is no magic. If a system touches a service, you can trace that relationship back to the line of code where you set it up.

## Architecture at a Glance

### Systems & Services

A **service** is a container of data or state (e.g. a collection of renderables, a render context manager). A **system** is a unit of logic that processes one or more services each frame.

The relationship between systems and services is many-to-many:

- A single system can consume multiple services.
- A single service can be processed by multiple systems.

This flexibility opens the door to unconventional (sometimes surreal) game design.

### No Privileged World

Most engines funnel everything through a single "world" or "scene" object that owns all entities and dictates what gets updated and rendered. Sencha has no such gatekeeper.

Any container of objects can have its own service and its own system to process it. A particle field, a UI layer, a debug overlay, a data-driven cutscene—none of these need to be actors in a world to participate in the frame. They register into a service, a system picks them up, and they render, collide, or behave alongside actors through the exact same mechanisms.

This means the boundary between "engine object" and "game object" is yours to draw, not the engine's. If something should exist, give it a service. If something should happen to it, give it a system. That's it.

## Engine Layers

The engine is organized into three layers, each with a strict downward-only dependency rule: **no layer depends on any layer above it.**

```
┌─────────────────────────────────────┐
│              Infuser                 │  ← Implementation layer
├─────────────────────────────────────┤
│               Teapot                  │  ← Mid-level abstractions
├─────────────────────────────────────┤
│              Kettle                 │  ← Bootstrap / Ring 0
└─────────────────────────────────────┘
```

### Kettle

Kettle is the foundation of the engine—ring 0. It provides the bootstrapping infrastructure that everything else is built on:

- **Logging** — `LoggingProvider`, `Logger`, log sinks (`ConsoleLogSink`, `FileLogSink`), and log levels.
- **Service hosting** — `ServiceHost`, `IService`, `ServiceProvider`, and the `BatchArray` container.
- **System hosting** — `SystemHost` and `ISystem`.

Kettle is purely structural. It defines interfaces and hosts but contains no concrete game logic. An application built with nothing but Kettle will still compile and run.

### Teapot

Teapot builds on Kettle with lower-level abstractions that most games will need but that don't assume a specific backend or game design:

- **Rendering** — `RenderSystem`, `RenderContextService`, `RenderContext`, `IRenderable`, and `IGraphicsAPI`.
- Base definitions for actors and components.
- Collision primitives.
- Some foundational service and system implementations.

Teapot answers the question *"what does a game need?"* without answering *"how does your game work?"*

### Infuser

*(Planned)* Infuser is the opinionated implementation layer. It supplies concrete rendering backends, component definitions, and other defaults that make it fast to get a game running. Everything in Infuser is opt-in—if you don't want the engine's assumptions, leave this layer out entirely and build on Teapot or Kettle directly.

## Building

Sencha uses **CMake** (≥ 3.20) and targets **C++20**.

```bash
cmake -S . -B build
cmake --build build
```

Tests are built automatically and can be run via CTest:

```bash
cd build
ctest --output-on-failure
```
