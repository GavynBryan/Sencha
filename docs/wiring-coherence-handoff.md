# Wiring Coherence & Service Ownership — Handoff

Branch: `claude/inspiring-johnson-1krl1o`
Status: two arcs, both implemented and pushed, **not yet compiled against the
full engine** (see Verification). The teardown-order invariant is the one thing
that must be validated on a real build — start there.

This is the working record for two connected refactors:

1. **Wiring coherence** — move the engine from service-location to explicit
   constructor injection; purge the `Engine&` god-object from contexts.
2. **Service-ownership dissolution** — delete `ServiceHost`/`IService` and own
   every service by name (singletons as `Engine` members; SDL and Vulkan as
   dependency-ordered groups).

The second completes the first: once wiring is explicit everywhere, the locator
that injection made redundant has no job left. Read this beside
`docs/core-systems-map.md`.

## Why this happened

An audit of `ServiceHost` found two wiring philosophies fighting:

- **Services and game systems** wired dependencies explicitly through
  constructor injection (the old `VulkanBootstrap` chain was the model).
- **The engine's own built-in systems** plus a god-object on every context
  resolved dependencies at use time, through `ctx.EngineInstance.Services()`.

`ServiceHost` also carried a large dead surface (a `ServiceProvider` view with
zero callers, tagged services, `GetAll`, `Remove*`, an interface-registration
overload — all tests-only), advertising a service-locator/DI architecture the
engine never adopted. And once injection was universal, the remaining
`Get<T>()` calls were all at wiring sites that could just hold a named
reference — so the locator contradicted the very rule we adopted.

The endpoint: **named ownership.** No container, no `IService` marker, explicit
wiring, teardown by member-declaration order.

## The architecture

### The boundary — three tiers

The discriminator that resolves every case is **lifetime**, not "does the frame
loop touch it" (the frame loop touches everything).

| Tier | What it is | How it's owned now |
| ---- | ---------- | ------------------ |
| **Foundation** | Substrate every tier needs in order to construct at all (logging) | `Engine` member, declared first → destroyed last; injected by reference |
| **Machinery** | The engine's own moving parts; lifetime must **bracket** the service set, or the Engine drives them each frame (schedule, zones, frame loop/driver, timing, worker lanes) | named `Engine::` member + accessor |
| **Service** | A capability the engine *has*; sits in a backend dependency chain needing ordered teardown, or wraps an external/optional backend (SDL, Vulkan, Renderer, Audio, Debug, Captions) | a named `Engine` member (singletons) or a dependency-ordered group struct (`PlatformServices`, `GraphicsServices`) |

Decisive cases:

- **Worker lanes** (`AsyncTaskQueue`, `ThreadPoolJobSystem`) are Machinery, not
  services, because they must be destroyed *before the entire service set*. A
  thing whose lifetime must contain the services cannot live inside them.
- **Renderer** is a Service despite being driven every frame, because it is the
  apex of an 18-deep Vulkan dependency chain that must tear down in reverse
  order. It is `GraphicsServices::MainRenderer`, declared last so it is
  destroyed first.
- **`LoggingProvider`** is Foundation: no engine deps, needed by every service's
  constructor, must outlive them all.

There is no longer an `IService` marker: a "service" is now anything owned in
the service region of `Engine` (the singleton members plus the two group
structs). The boundary is the ownership location, made legible by name.

### The wiring law

> **Resolve at the wiring site. Inject the reference. Never resolve at the use
> site.**

Corollaries:

- Constructors take what they need; you learn a type's dependencies by reading
  its constructor (or, for a group, by reading the struct top to bottom).
- Only the integration root — `Engine`, the group constructors, frame-phase
  registration — assembles the graph, once.
- Steady-state code holds the references it was handed. Access is by name
  (`engine.Graphics().Swapchain`), compile-checked, no runtime type lookup.
- Contexts carry per-call **data**, never an engine handle.

`GameContexts.h` and `Game.h` state the context half inline; `IService` is gone,
and the service boundary now lives in the `Engine` member layout and the group
structs.

## What changed (by commit)

Arc 1 — wiring coherence:

| Commit | Change |
| ------ | ------ |
| `568b120` | Delete `ServiceProvider`; remove the dead `ServiceHost` API (tagged, `GetAll`, `Remove*`, interface overload); collapse the registry to `type -> IService*`. |
| `1666474` | Promote `LoggingProvider` to an `Engine` Foundation member; `ServiceHost` becomes services-only. |
| `605ac02` | Write the (then-current) service boundary contract onto `IService`. |
| `1e52ea0` | Constructor-inject `AudioSystem`/`CaptionSystem` (defaulted pointer — keeps the headless-test seam); `DefaultRenderPipeline` caches the swapchain at its wiring point. |
| `86982f1` | Remove `Engine& EngineInstance` from all eleven contexts; `Game` gains `AttachEngine`/`GetEngine`. |

Arc 2 — service-ownership dissolution:

| Commit | Change |
| ------ | ------ |
| `3dba55a` | Singletons (`Debug`/`Audio`/`Captions`) → named `Engine` members (`unique_ptr`, emplaced in `Initialize`, reset in reverse on teardown). |
| `7163a79` | SDL → `PlatformServices` group (`Video`, `Windows`); `SdlBootstrap` deleted. |
| `077db46` | Vulkan chain → `GraphicsServices` group (dependency-ordered members; delegating ctor builds the policy + chain; `IsValid()` folds the old chain check); `VulkanBootstrap` deleted. |
| `5c0f30c` | Delete `ServiceHost` + `IService`; strip the base from 27 types; remove `Engine::Services()`; teardown is now pure member-declaration order. |

### Two design notes worth keeping

- **Defaulted-pointer injection** for `AudioSystem`/`CaptionSystem`: the service
  is a constructor pointer defaulted to `nullptr`. The engine always injects;
  `nullptr` is the headless-test seam (the engine-free `Update()` core, incl. a
  deliberate "no audio device" test). Member is `AudioBackend`, not `Audio`,
  because `Audio` is the phase-method name.
- **No facade for the Game**: `Game::OnStart` legitimately touches the whole
  engine — it is the integration partner. It holds the engine via a one-time
  `AttachEngine` bind and reaches it through `GetEngine()`; contexts stay pure
  data. No new interface.
- **`Renderer MainRenderer;`**: a member may not share its type's name (`Renderer
  Renderer;` is ill-formed — `-Wchanges-meaning`). Only that member collided.

## Verification

This environment has **no SDL3, no Vulkan, and no network**, and every test
target links the full `sencha_engine` library, so the full engine and test
suite could not be built here.

Verified locally (compiled and run, standalone):

- The original `ServiceHost` core (before deletion).
- The **same-name-member** check that caught `Renderer Renderer;` (ill-formed)
  → renamed to `MainRenderer`.
- The **delegating-ctor + static-policy-helper + cross-referencing init-list**
  pattern that `GraphicsServices` uses.

Everything that includes SDL/Vulkan headers is **hand-reviewed but uncompiled**.
Build locally with a Vulkan SDK + SDL3 (`cmake --preset dev`).

## Build-watch (in priority order)

1. **Teardown order is the whole risk.** It is now encoded in member
   declaration order. Inside `GraphicsServices`, the Vulkan services are
   declared in dependency order with `MainRenderer` last (destroyed first);
   `GraphicsState` is declared after `PlatformState`, so Vulkan tears down
   before the SDL window. **Validate with Vulkan validation layers** — a wrong
   order is a use-after-free in shutdown, and it is the one thing that cannot be
   checked without a GPU build.
2. **`GraphicsServices` init list** mirrors the deleted `VulkanBootstrap::
   Install` argument-for-argument (18 constructions transcribed by hand). Worth
   a read against `git show 077db46^:engine/src/graphics/vulkan/VulkanBootstrap.cpp`.
3. **Removed includes** across the systems/pipeline files — each symbol was
   traced to another include, but a transitive surprise is a likely small
   failure. Fix is a one-line `#include`.
4. **Conditional presence:** `GraphicsState`/`Graphics()` are
   `#ifdef SENCHA_ENABLE_VULKAN`-gated (the `unique_ptr` deleter needs the
   complete type). `SENCHA_ENABLE_VULKAN` is a `PUBLIC` definition, so the
   `Engine` layout is consistent across consumers.

## Open threads / not done

- **Naming:** `CaptionRuntime` connotes Machinery (collides with `ZoneRuntime`)
  but is a service. Renaming (`CaptionService`) would make the tier legible from
  the identifier. Low value, rename churn — not done.
- **CubeDemo `SetRelativeMouseMode`** resolves `SdlWindowService` via
  `GetEngine().Platform().Windows` on each mouse event. Event-time, through the
  bound engine (not a context god-object), so it honors the law. A stricter form
  caches the window service at `OnStart`. Left as-is — example code.
- **Asset ownership:** `RuntimeAssets` is game-owned in CubeDemo (a known gap in
  `core-systems-map.md`). Adjacent but a separate concern.
- **Runtime dependency graph (dropped):** a debug-menu view of the service graph
  was considered and dropped. If revived, it re-justifies recording nodes/edges
  at the wiring site — see the discussion that produced this refactor.

## Where to look in the code

| Topic | File |
| ----- | ---- |
| Service ownership + teardown order | `engine/include/app/Engine.h` (member order), `engine/src/app/Engine.cpp` (`Initialize`/`Shutdown`/fail-init, accessors) |
| Vulkan group (the dependency graph, in source) | `engine/include/graphics/vulkan/GraphicsServices.h`, `engine/src/graphics/vulkan/GraphicsServices.cpp` |
| SDL group | `engine/include/platform/PlatformServices.h` |
| Context contract | `engine/include/app/GameContexts.h` (header note), `engine/include/app/Game.h` (`AttachEngine`/`GetEngine`) |
| Built-in system injection | `engine/src/app/Engine.cpp` (`Initialize`) |
| Frame-phase wiring (capture-once pattern) | `engine/src/app/EngineFramePhases.cpp` |
