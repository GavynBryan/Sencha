# Wiring Coherence — Handoff

Branch: `claude/inspiring-johnson-1krl1o`
Status: implemented across 5 commits, pushed, **not yet compiled against the
full engine** (see Verification). Pick up from "Build-watch" and "Open
threads".

This document is the working record for the wiring-coherence pass: why it
happened, the architecture it establishes, what changed, and what is
deliberately left open. Read it beside `docs/core-systems-map.md`.

## Why this exists

An audit of `ServiceHost` found the engine had two wiring philosophies fighting
each other:

- **Services and game systems** wired dependencies explicitly through
  constructor injection (the Vulkan chain in `VulkanBootstrap` is the model).
- **The engine's own built-in systems** plus a god-object on every context
  resolved dependencies at use time, through `ctx.EngineInstance.Services()`.

On top of that, `ServiceHost` carried a large dead surface (a `ServiceProvider`
view with zero production callers, tagged services, `GetAll`, `Remove*`, an
interface-registration overload — all tests-only) that advertised a
service-locator/DI architecture the engine never adopted. `LoggingProvider` sat
inside `ServiceHost` as a privileged non-service member — the same
first-class-vs-bag ambiguity the host's boundary is meant to remove, one level
down.

The goal was coherence: one wiring rule, a self-documenting boundary, no dead
surface, without lobotomizing the parts that earn their place (notably the
host's LIFO teardown, which the Vulkan stack relies on).

## The architecture

### The boundary — three tiers, one marker

The discriminator that resolves every case is **lifetime**, not "does the frame
loop touch it" (the frame loop touches everything).

| Tier | What it is | Owner | In-code signal |
| ---- | ---------- | ----- | -------------- |
| **Foundation** | Substrate every tier needs in order to construct at all (logging) | `Engine`, declared first | injected by reference; owned by nobody below it |
| **Machinery** | The engine's own moving parts; lifetime must **bracket** the service set, or the Engine drives them directly each frame (schedule, zones, frame loop/driver, timing, worker lanes) | `Engine`, named members | a named `Engine::` member + accessor |
| **Service** | A capability the engine *has*; sits in a backend dependency chain needing ordered teardown, or wraps an external/optional backend (SDL, Vulkan, Renderer, Audio, Debug, Captions) | `ServiceHost` | `: public IService` |

Decisive cases:

- **Worker lanes** (`AsyncTaskQueue`, `ThreadPoolJobSystem`) are Machinery, not
  services, because they must be destroyed *before the entire service set*
  (`Engine::Shutdown` joins them before `ServiceRegistry.Clear()`). A thing
  whose lifetime must contain the services cannot live inside them.
- **Renderer** is a Service despite being driven every frame, because it is the
  apex of an 18-deep Vulkan dependency chain that must tear down in reverse
  order — its lifetime is *inside* the service set.
- **`LoggingProvider`** is Foundation: no engine deps, needed by every service's
  constructor, must outlive them all.

The self-documenting marker is `: public IService`. Its contract is written on
the base class in `engine/include/core/service/IService.h` — read that first.

### The wiring law

> **Resolve at the wiring site. Inject the reference. Never resolve at the use
> site.**

Corollaries:

- Constructors take what they need; you learn a type's dependencies by reading
  its constructor.
- Only the integration root — `Engine`, the bootstraps, frame-phase
  registration — calls `Get<T>()`, and it does so once, capturing references.
- `ServiceHost::Get/TryGet` are **wiring-time tools**, not ambient runtime
  authority. Steady-state code holds the references it was handed.
- Contexts carry per-call **data**, never an engine handle.

`GameContexts.h` and `Game.h` state this contract inline.

## What changed (by commit)

| Commit | Phase | Change |
| ------ | ----- | ------ |
| `568b120` | 0 | Delete `ServiceProvider`; remove tagged API, `GetAll`, `Remove*`, the interface overload; collapse the registry from `type -> vector<IService*>` to `type -> IService*`; rewrite the `ServiceHost` doc comment; trim tests; fix stale `ServiceProvider` references in logging docs. |
| `1666474` | 1 | Promote `LoggingProvider` to an `Engine` Foundation member (`Engine::Logging()`), declared before `ServiceRegistry` so teardown order is correct by construction. `ServiceHost` no longer owns or exposes logging. `VulkanBootstrap::Install` takes `LoggingProvider&` explicitly. |
| `605ac02` | 2 | Write the service boundary contract onto `IService`. |
| `1e52ea0` | 3 | Constructor-inject `AudioSystem`/`CaptionSystem` (defaulted pointer — see below). `DefaultRenderPipeline` caches `VulkanSwapchainService` at `AddMeshRenderFeature` instead of reaching through the context each extract. |
| `86982f1` | 4 | Remove `Engine& EngineInstance` from all eleven contexts. `Game` gains `AttachEngine` (called once by `Engine::Run`) and protected `GetEngine()`. CubeDemo, both test games, frame phases, `Engine::Run`, and the parallelization doc follow the contract. |

### Why a defaulted pointer for the audio systems

`AudioSystem`/`CaptionSystem` take their service(s) as a constructor pointer
defaulted to `nullptr`. The engine always injects a valid pointer; `nullptr` is
the existing **headless-test seam** — the "engine-free core" `Update()` is
driven directly by tests (including a deliberate "no audio device at all"
caption-degrade test). Mandatory reference injection would have broken that
seam, which is itself good design worth keeping. The member name is
`AudioBackend`, not `Audio`, because `Audio` is the phase-method name.

### Why no facade for the Game

`Game::OnStart` legitimately touches `Services()`, `Tasks()`, `Zones()`,
`Runtime()`, `Timing()`, `GetRenderPipeline()` — it is the integration partner.
Replacing the context's `Engine&` with a view that re-exposes the same surface
would be Engine-by-another-name (indirection without reduction). Instead the
Game holds the engine via a one-time `AttachEngine` bind and reaches it through
`GetEngine()`; the contexts become pure data. No new interface.

## Verification

This environment has **no SDL3, no Vulkan, and no network** (FetchContent for
gtest/VMA/glslang/imgui cannot run), and every test target links the full
`sencha_engine` library. So the full engine and test suite could not be built
here. The CI preset note confirms a headless no-Vulkan build is not supported.

Verified locally:

- `ServiceHost` core — **compiled and run** in a throwaway TU: `AddService`
  arg forwarding, `Get`/`TryGet`/`Has`, throw-on-missing, `Clear`. Passes.
- `IService.h`, `ServiceHost.h`, `LoggingProvider.h`, `Logger.h` —
  `g++ -std=c++20 -fsyntax-only`. Pass.

Everything that includes SDL/Vulkan headers (the systems, contexts, `Engine`,
`EngineFramePhases`, CubeDemo, runtime/audio tests) is **hand-reviewed but
uncompiled**. Build locally with a Vulkan SDK + SDL3 (`cmake --preset dev`).

## Build-watch (most likely snags when you compile)

1. **Removed includes.** `<app/Engine.h>` and `<core/service/ServiceHost.h>`
   were dropped from `AudioSystem.cpp`, `CaptionSystem.cpp`, and
   `DefaultRenderPipeline.cpp` (they no longer reach through the engine). Each
   symbol they still use was traced to another include, but a transitive
   surprise is the single most likely failure. Fix is a one-line `#include`.
2. **`EngineScheduleTests.cpp`** now has an unused `<app/Engine.h>` (the harness
   no longer constructs an `Engine`). Harmless; left in to avoid guessing.
3. **Non-Vulkan builds:** `DefaultRenderPipeline::Swapchain` is an unused
   private field. No `-Werror` in the build, so it is only a warning.
4. **Designated initializers:** contexts now lead with `.Config`. If you add a
   field, keep declaration order matching initialization order.

## Open threads / not done

- **Phase 5 (naming):** `CaptionRuntime` lives in the host but its name connotes
  Machinery (collides with `ZoneRuntime`). Once `: public IService` is the real
  signal this matters less; aligning it (`CaptionService`) would make tier
  legible from the identifier. Optional, low value, rename churn — not done
  blind.
- **CubeDemo `SetRelativeMouseMode`** still resolves `SdlWindowService` via
  `GetEngine().Services()` on each mouse event. Event-time, through the bound
  engine (not a context god-object), so it honors the law. A stricter form
  caches the window service at `OnStart`. Left as-is — it is example code and
  the extra state did not seem worth it.
- **Asset ownership:** `RuntimeAssets` is game-owned in CubeDemo (a known gap in
  `core-systems-map.md`). Adjacent to wiring but a separate concern — out of
  scope here.
- **`ServiceHost` vs `EngineSchedule`:** both have a typed `Get<T>`. They are
  deliberately *not* merged — they own different tiers (services vs systems),
  and `EngineSchedule` additionally topo-sorts and dispatches phases. The
  duplication is now justified by the taxonomy rather than accidental.

## Where to look in the code

| Topic | File |
| ----- | ---- |
| Service boundary contract | `engine/include/core/service/IService.h` |
| The host (ownership + LIFO + typed lookup) | `engine/include/core/service/ServiceHost.h` |
| Foundation ownership + teardown order | `engine/include/app/Engine.h` (member order), `engine/src/app/Engine.cpp` (`Shutdown`, fail-init) |
| Context contract | `engine/include/app/GameContexts.h` (header note), `engine/include/app/Game.h` (`AttachEngine`/`GetEngine`) |
| Built-in system injection | `engine/src/app/Engine.cpp` (`Initialize`) |
| Frame-phase wiring (capture-once pattern) | `engine/src/app/EngineFramePhases.cpp` |
