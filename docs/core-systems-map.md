# Sencha Core Systems Map

Snapshot: 2026-06-12.

This is a reader's map of the engine as it exists in this tree. It is meant to
sit beside static analysis: use it to understand which systems own what, which
dependencies are intended, and which constraints should make you suspicious
when new code appears.

The shortest version:

- `app/Engine` is the integration root. It owns services, the frame loop, the
  schedule, zone runtime, timing, and the two worker lanes.
- `World` is the ECS storage unit. `Registry` wraps a `World` plus
  registry-local resources. `ZoneRuntime` owns the global registry and loaded
  zone registries.
- A frame is built by a fixed phase pipeline in `FrameDriver`. Game systems
  are ordinary C++ objects registered into `EngineSchedule` by the methods
  they implement.
- Zone streaming uses publish-by-handoff: build a detached registry on an
  async task thread, then attach/finalize it on the main thread during
  `DrainAsyncTasks`.
- Assets use the same split: stage bytes/decoded CPU data off-thread, commit
  into caches and GPU resources on the owner thread.
- Render extraction copies simulation state into transient render-domain data:
  camera data, `RenderQueue`, and packet flags. Vulkan features draw from that
  staged data, not from live ECS state.
- Audio source components are driven only for registries in the audio view, so
  dormant preloaded zones are silent by construction.

## Orientation

High-value files to read first:

| Topic | Start here |
| ----- | ---------- |
| Process and engine lifetime | `engine/include/app/Application.h`, `engine/src/app/Engine.cpp` |
| Frame order | `engine/include/runtime/FrameDriver.h`, `engine/src/app/EngineFramePhases.cpp` |
| System registration | `engine/include/app/EngineSchedule.h`, `engine/include/app/GameContexts.h` |
| ECS storage | `engine/include/ecs/World.h`, `engine/include/ecs/Query.h`, `docs/ecs/overview.md` |
| Zones | `engine/include/zone/ZoneRuntime.h`, `engine/include/zone/AsyncZoneLoader.h` |
| Transforms | `engine/src/world/transform/TransformPropagation.cpp` |
| Scene files | `engine/src/world/serialization/SceneSerializer.cpp`, `engine/src/world/serialization/SceneFieldCodec.cpp` |
| Asset front door | `engine/include/core/assets/AssetSystem.h`, `engine/include/core/assets/AssetLoader.h` |
| Asset streaming | `engine/include/core/assets/AssetPreloader.h`, `engine/src/core/assets/AssetPreloader.cpp` |
| Render extraction | `engine/src/app/DefaultRenderPipeline.cpp`, `engine/src/render/RenderExtractionSystem.cpp` |
| Vulkan backend | `engine/src/graphics/vulkan/VulkanBootstrap.cpp`, `engine/src/graphics/vulkan/Renderer.cpp` |
| Audio runtime | `engine/include/audio/AudioSourceComponent.h`, `engine/src/audio/AudioSystem.cpp` |
| Concurrency contracts | `engine/include/jobs/JobSystem.h`, `engine/include/jobs/AsyncTaskQueue.h`, `docs/ecs/parallelization.md` |

## Top-Level Ownership

The runtime ownership shape looks like this:

```text
Application
  Engine
    EngineConfig value
    ServiceHost
      LoggingProvider
      DebugService
      AudioService
      SDL window/video services
      Vulkan services
      Renderer
    RuntimeFrameLoop
    FrameDriver
    EngineSchedule
      DefaultRenderPipeline
      AudioSystem
      game systems
    ZoneRuntime
      global Registry
      zone Registries
    AsyncTaskQueue
    ThreadPoolJobSystem
    TimingHistory

Game-owned in CubeDemo
  RuntimeAssets
    AssetRegistry
    TextureCache
    MaterialCache
    StaticMeshCache
    AudioClipCache
    AssetSystem
  AssetPreloader
  AsyncZoneLoader
  scene-specific handles and system state
```

`Engine` intentionally does not own every gameplay-facing object. In CubeDemo,
the game owns `RuntimeAssets`, `AssetPreloader`, and `AsyncZoneLoader` because
those are content/runtime policy objects. The engine owns the lower-level
services and frame machinery.

Service lifetime is explicit. `ServiceHost` destroys services in reverse
insertion order. Vulkan services are installed in dependency order, and
`Renderer` is added last so it tears down first, before the services and device
objects used by render features. `Engine` destroys worker/task queues before
zones and services, so pending work cannot keep reaching into torn-down state.

## Frame Contract

`FrameDriver` owns the outer loop. The default engine phases are registered by
`RegisterDefaultEngineFramePhases`.

| Phase | What happens |
| ----- | ------------ |
| `PumpPlatform` | Begin input frame, poll SDL, let `Game::OnPlatformEvent` consume events, update `InputFrame`, set quit/pause flags. |
| `ResolveLifecycle` | Apply window resize/minimize/restore to `RuntimeFrameLoop`. |
| `RebuildGraphics` | Recreate swapchain/frame/render resources when needed. |
| `DrainAsyncTasks` | Run ready async commits on the owner thread, within `AsyncCommitBudgetMs`. Zone attaches and asset publishes happen here. |
| `ScheduleTicks` | Build `FrameRegistryView` from zone participation and decide whether fixed simulation runs this frame. |
| `Simulate` | Run fixed-phase systems, physics systems, transform propagation, then post-fixed systems. |
| `Update` | Run presentation-rate systems and then audio systems. |
| `ExtractRenderPacket` | Propagate visible transforms, extract camera and render queue data. |
| `Render` | Submit the current packet through `Renderer`. |
| `EndFrame` | Run cleanup/end systems, record lifecycle timing, end runtime frame, flip packet buffers, pace. |

Current simulation scheduling is conservative: lifecycle-only frames run zero
fixed ticks, normal frames currently schedule one fixed tick. Fixed systems
consume `FixedSimTime`; `FrameUpdateContext::WallDeltaSeconds` is for
presentation-rate behavior only, such as camera look smoothing or UI.

Lifecycle-only frames are real frames. They still pump platform, drain async
tasks, and run end-frame bookkeeping, but skip extraction/render when the
surface is not renderable.

## Scheduling Model

`EngineSchedule` owns scheduled systems. A system is any type with one or more
phase methods:

- `FixedLogic(FixedLogicContext&)`
- `Physics(PhysicsContext&)`
- `PostFixed(PostFixedContext&)`
- `FrameUpdate(FrameUpdateContext&)`
- `ExtractRender(RenderExtractContext&)`
- `Audio(AudioContext&)`
- `EndFrame(EndFrameContext&)`

Registration must happen before `EngineSchedule::Init()`. Per-phase ordering is
topologically sorted from `schedule.After<T, TDep>()`; dependencies only apply
inside phases where both systems participate.

The built-in systems registered by `Engine::Initialize()` are:

- `DefaultRenderPipeline`
- `AudioSystem`

Game systems are registered from `Game::OnRegisterSystems`.

## ECS, Registries, And Zones

`World` is the archetype ECS:

- Entities are generational `EntityId`s.
- Components get `ComponentId`s at registration time.
- Component signatures select archetypes.
- Archetype chunks store SoA columns in 16 KB blocks.
- Queries cache matching archetypes and iterate chunks.
- Change detection is chunk-conservative, based on per-column last-written
  frame counters.

`Registry` wraps a `World` as `registry.Components`, a migration-only
`registry.Entities` facade, and a `ResourceRegistry`. A registry is the unit of
isolation for zones and zone-parallel work.

`ZoneRuntime` owns:

- one global registry, addressable as `FrameRegistryView::Global`
- N loaded zone registries, each with a `ZoneId`
- per-zone `ZoneParticipation`

`BuildFrameView()` creates four zone spans:

- `Visible`
- `Physics`
- `Logic`
- `Audio`

These spans contain only participating zone registries. The global registry is
separate. Systems that want global state need to read `view.Global` explicitly.

A zone with no participation is dormant. It is attached and queryable by tools
or game code, but absent from all frame spans, so it cannot affect simulation,
render, physics, or audio.

## ECS Constraints

Treat these as hard rules:

- Register all component types before the first entity is created in a `World`.
- Do not call direct structural APIs (`CreateEntity`, `DestroyEntity`,
  `AddComponent`, `RemoveComponent`) while a query or lifecycle hook is active.
  Use `CommandBuffer` inside systems and flush at explicit boundaries.
- Lifecycle hooks may retain/release external handles or stop voices, but must
  not perform structural ECS mutation.
- Do not store owning runtime resources directly in components. Current chunk
  storage relocates component bytes; prefer small trivially copyable component
  data, handles, IDs, and registry/world resources.
- Non-const `TryGet<T>` and `ForEachComponent<T>` count as writes and bump
  change detection. Use const access for pure reads.
- Do not cache chunk row pointers across structural changes unless the cache is
  invalidated by `World::StructuralVersion()`.

The transform system is the main example of the last rule. It keeps a
`PropagationOrderCache` world resource with parent-before-child entries and
cached row pointers. It rebuilds when structural version changes or
`Changed<Parent>` reports a hierarchy edit.

## Scene Serialization

Scene serialization is schema-driven:

- `TypeSchema<T>` describes the authored fields of a component.
- `Field` descriptors name members and can carry defaults/optional flags.
- `ComponentSerializer<T>` saves/loads a component by visiting its schema.
- `SceneFieldCodec<T>` handles special field types, especially runtime handles
  that serialize as asset paths.
- `ComponentStorageTraits<T>` maps a serializable component to a binary chunk
  ID and defines how it is inserted into registry storage.

`InitSceneSerializer()` registers the built-in serializable components:

- `LocalTransform` under JSON key `Transform`
- `CameraComponent` under `Camera`
- `StaticMeshComponent` under `StaticMesh`
- `AudioSourceComponent` under `AudioSource`

JSON scene format:

- root object with `version`, `entities`, and `hierarchy`
- entities are saved in alive-entity order
- each entity has a `components` object keyed by serializer JSON key
- hierarchy entries use entity array indices, not serialized runtime IDs

Binary scene format exists and uses chunk IDs, but asset-backed handle codecs
currently reject binary serialization. In practice, scene assets with meshes,
materials, or audio clips use JSON until binary handle encoding is implemented.

`SceneSerializationContext` is the dependency channel for serialization:

- `LoggingProvider` is required.
- `AssetSystem` is required when serializing/deserializing asset-backed handles.

Runtime fields should be omitted from `TypeSchema`. For example,
`AudioSourceComponent::Voice` and `Started` are runtime-only and reset on load.

## Asset Pipeline

Asset identity is currently virtual path based:

```text
asset://meshes/dev/cube.smesh
asset://materials/dev/red.smat
asset://audio/ambient.wav
```

`AssetRegistry` maps virtual paths to `AssetRecord`s. The directory scanner
registers runtime formats only:

- `.smesh` -> static mesh
- `.smat` -> material
- `.stex` -> texture
- `.smap` -> scene
- `.sclip` -> audio

Source formats such as PNG, glTF, Blender, WAV, and OGG are handled by the
dev-only cook/import layer when `SENCHA_ENABLE_COOK` is enabled. The scanner
skips `.cooked/`.

`AssetSystem` is the front door for runtime code:

- resolves a path through `AssetRegistry`
- deduplicates against caches
- runs `LoadStaged` and `CommitTyped` synchronously for direct loads
- exposes loaders for async drivers
- provides `TryAcquire*` and `Release*` helpers for cached-only paths

The staged-load contract is the core asset rule:

```text
LoadStaged(record, source)
  file IO + decode into plain CPU data
  no caches, no services, no Vulkan
  may run on an async task thread

Commit(staging)
  owner thread only
  inserts into cache
  performs GPU upload if needed
  returns/retains runtime handles
```

Caches are path-keyed, ref-counted, and generational. They are owner-thread
objects, not thread-safe containers. Component lifecycle hooks retain and
release cache handles so entity lifetime owns asset references.

Materials are the important dependency case. A `.smat` stage parses a material
description. Its commit resolves texture references through `AssetSystem`, so
manifest preloading loads textures first and submits materials second.

`AssetPreloader` drives manifest batches through the async lane:

- wave 1: leaf assets such as textures, meshes, and audio clips
- wave 2: materials, after wave 1 commits
- in-flight table coalesces duplicate path requests
- held handles keep committed assets alive until the real consumer takes its
  own references
- failures count toward completion; final scene load has a synchronous fallback

`AssetManifest` is derived data, not authored data. `CollectAssetPaths` walks a
JSON tree and collects unique `asset://` strings without knowing component
schemas.

## Zone Loading

`AsyncZoneLoader` is the first complete async consumer.

Flow:

1. Main thread reserves a `RegistryId`.
2. Async work creates a detached `Registry` with that ID and calls the build
   callback.
3. Commit runs at `DrainAsyncTasks`.
4. If an `AssetPreload` is attached and not complete, the registry attach is
   deferred until the preload's final commit.
5. The registry is attached to `ZoneRuntime`.
6. Finalize runs on the owner thread.
7. Preload scaffolding handles are released.
8. `ZoneLoad` discontinuity is marked only if initial participation is nonzero.

Build callback rules:

- may mutate only the detached registry it was given
- must not touch service host, asset caches, Vulkan, audio service, or live
  zones
- may store plain cache/service pointers as registry resources if they are not
  dereferenced off-thread

Finalize callback rules:

- runs on the owner thread
- may deserialize scene JSON, resolve assets, touch services, and do game
  wiring
- runs after attach but before the frame can observe the zone

Dormant preloading is the intended room-streaming path: attach with no
participation, then later flip participation live in game code.

## Render And Graphics

The render path has three layers:

1. Scene/render-domain data in `render/`: camera components, mesh components,
   material data, render queue, extraction.
2. Vulkan backend services in `graphics/vulkan/`: instance, surface, device,
   queues, allocator, buffers, images, descriptors, pipeline/shader caches,
   swapchain, frame service, scratch buffers, renderer.
3. Render features, such as `MeshRenderFeature`, which bridge render-domain
   data to Vulkan commands.

`DefaultRenderPipeline` owns the built-in render queue and camera render data.
The built-in mesh feature receives references to those extracted objects, while
`RenderPacket` carries frame/presentation metadata and renderable flags. During
`ExtractRenderPacket`, the pipeline:

- finds the first active camera in visible registries
- builds Vulkan-style camera matrices and frustum
- extracts static mesh components into `RenderQueue`
- sorts opaque items
- writes camera/renderable flags into `RenderPacket`

`RenderExtractionSystem` copies data out of ECS. It intentionally emits
`RenderQueueItem` values, not pointers into chunks, so structural changes after
extraction cannot invalidate draw submission.

`Renderer` owns `IRenderFeature`s. Features receive `RendererServices` during
`Setup()` and cache direct pointers. They should not use `ServiceHost` in the
hot draw path.

`Renderer::DrawFrameScheduled()` owns:

- acquire swapchain image
- rotate frame scratch
- record render phases
- submit and present
- return structured lifecycle results to `RuntimeFrameLoop`

`VulkanFrameService` owns per-frame command pools, semaphores, fences, present
wait handling, and deletion queue advancement. Swapchain instability is reported
as lifecycle state, not leaked into simulation time.

## Audio

`AudioService` is an SDL-backed service. It owns:

- audio device
- bus table from `EngineAudioConfig`
- built-in `Engine` bus
- voice slots and SDL audio streams

It does not own clips. Clips live in `AudioClipCache`. `AudioService::Play`
copies data into SDL streams and returns a generational `VoiceId`.

`AudioSourceComponent` is a scene-authored emitter:

- serialized fields: clip, bus, gain, pan, looping, play-on-active
- runtime fields: voice, started
- component trait `OnAdd` retains the clip
- component trait `OnRemove` stops the voice, then releases the clip

The invariant is: a voice never outlives the clip reference that feeds it.

`AudioSystem` runs during `FramePhase::Update` over `FrameRegistryView::Audio`:

1. tick `AudioService` to retire drained voices
2. sweep voices for registries no longer in the audio view
3. visit active `AudioSourceComponent`s and apply start rules

Dormant zones are silent because they are absent from the audio span. There is
no spatialization yet beyond authored pan, and streamed music is not part of
this slice.

## Platform And Input

SDL integration is split:

- `SdlBootstrap` installs video/window services.
- `SdlWindowService` owns windows and window state.
- `SdlInputCapture` translates SDL events into `InputFrame`.

`Game::OnPlatformEvent` sees each SDL event before input capture. If it marks
the event handled, the default input path skips it.

`InputFrame` has held state plus edge lists. Edges are drained on the first
fixed tick of a frame. If a frame runs zero fixed ticks, edges persist so input
impulses are not lost during lifecycle frames or stalls.

There is no engine-owned action binding layer right now. Rebindable gameplay
controls should be added as a mapper over `InputFrame`, not as a second SDL
event pump.

## Concurrency

There are two worker lanes. Do not mix their contracts.

`JobSystem` / `ThreadPoolJobSystem` is the frame lane:

- fork/join inside the current phase
- caller participates
- zero workers means deterministic inline execution
- no nested `ParallelFor`
- one active `ParallelFor` per pool
- jobs must not throw
- jobs must not touch ambient mutable engine state

`ForEachRegistryParallel` is the safe current helper because registries are
disjoint. It runs one job per registry and falls back to inline for zero or one
registry.

`AsyncTaskQueue` is the cross-frame lane:

- work runs on task threads
- commit runs on owner thread during `DrainAsyncTasks`
- owner-thread APIs are `Submit`, `Cancel`, `DrainCompletions`, `PumpWork`
- zero workers is test mode only; `PumpWork` runs work inline
- commits are atomic and cannot be split by the drain budget
- work and commit must not throw

Async work should produce plain data. Engine state is re-entered only through
the commit callback.

Runtime knobs in `EngineRuntimeConfig`:

| Field | Purpose |
| ----- | ------- |
| `JobWorkerCount` | `-1` auto, `0` deterministic inline, positive fixed worker count. |
| `AsyncTaskThreadCount` | Dedicated async task threads. Engine config requires at least one. |
| `AsyncCommitBudgetMs` | Soft owner-thread commit budget. First ready commit always runs. |
| `ZoneParallelPropagation` | Optional zone-axis transform propagation. Off by default. |
| `FixedTickRate` | Simulation tick rate. |
| `TargetFps` | Optional wall-frame pacing cap. |

## Dependency Directions

Think of the modules as layers with a few intentional integration modules:

```text
app
  -> runtime, zone, jobs, platform, graphics, render, audio, debug, core

runtime
  -> time, input, render packet, frame registry views

zone
  -> world/registry, jobs async queue, runtime discontinuities

world
  -> ecs, math, core metadata/serialization
  -> render/audio only in built-in serialization and default registry helpers

ecs
  -> standard library data structures only

assets
  -> core assets/serialization/logging
  -> render/audio/graphics caches through AssetSystem and loaders

render
  -> math, ecs/world data, asset handles/caches
  -> graphics/vulkan only for render features and current camera extent type

graphics/vulkan
  -> core services/logging, platform window/surface, Vulkan
  -> renderer owns render features, but does not know ECS

audio
  -> core config/logging/services, SDL
  -> world/ecs only for AudioSourceComponent and AudioSystem

platform/input
  -> SDL, core services/config

math
  -> no engine dependencies
```

Important nuance: `core/assets` is an integration layer, not pure low-level
core. It knows about render, graphics texture caches, and audio clip caches.
Do not use its dependency shape as permission for other low-level `core/`
systems to know about gameplay or backend modules.

## Adding New Things

New component:

1. Define plain data near the owning feature.
2. Register it before any entity is created in each registry that may use it.
3. Add `TypeSchema<T>` only for authored fields.
4. Add `ComponentTraits<T>` only if lifetime hooks are needed.
5. If it serializes, add `ComponentStorageTraits<T>`, a unique `SceneChunk`
   FourCC, and register it from `InitSceneSerializer()` or the owning module.
6. If it holds asset handles, retain on add and release on remove through a
   world resource that stores cache pointers.

New asset type:

1. Add/confirm an `AssetType` and registry extension.
2. Add a cache or runtime store with generational handles if residency matters.
3. Implement `IAssetLoader::LoadStaged` and owner-thread `CommitTyped`.
4. Wire it into `AssetSystem::Load*`, `TryAcquire*`, `Release*`, and `LoaderFor`.
5. Extend `AssetPreloader` if the type should stream from manifests.
6. Keep importers cook/dev-only if they parse source formats not meant to ship.

New system:

1. Pick the phase based on the data contract, not convenience.
2. Use the context's active registry span.
3. Avoid global registry mutation inside registry-parallel work.
4. Use `CommandBuffer` for structural changes during ECS iteration.
5. Register before schedule init, and declare phase-local order with `After`
   only when there is a real dependency.

New render feature:

1. Keep scene extraction separate from draw submission.
2. Add render queue or packet data if the renderer needs new copied state.
3. Implement `IRenderFeature`.
4. Cache Vulkan service pointers during `Setup()`.
5. Release GPU resources in `Teardown()`.

## Sharp Edges And Current Gaps

- Binary scene serialization does not support asset-backed runtime handles yet.
- `AssetSystem` logs `Generated` and `Embedded` sources as not implemented for
  current asset types.
- Runtime asset stores are game-owned in CubeDemo, not engine-owned.
- Asset caches and Vulkan services are owner-thread objects.
- No chunk-parallel query API exists yet.
- No concurrent execution of multiple systems inside one phase exists yet.
- Transform propagation can run zone-parallel, but is serial by default because
  the target workload is a few room-sized zones.
- `DefaultRenderPipeline` uses the first active camera it finds in visible
  registries.
- Transparent materials currently warn and render through the opaque path.
- Audio has no listener/spatial attenuation and no streamed music path yet.
- Debug UI requires Vulkan and `SENCHA_ENABLE_DEBUG_UI`.
- Cook/import code is dev/tooling code behind `SENCHA_ENABLE_COOK`; do not let
  source-format import dependencies become required shipping runtime state.

## Architectural Smells

Be cautious when you see:

- a task-thread lambda capturing `ServiceHost`, caches, `Renderer`, or live
  registries by reference
- worker jobs writing global or shared mutable state without explicit merge
  slots
- a component containing `std::string`, `std::vector`, owning pointers, or
  backend resources
- a lifecycle hook calling structural ECS APIs
- direct `World::AddComponent` or `DestroyEntity` inside query iteration
- a renderer or Vulkan service reading live ECS state during draw
- a service resolving sibling services through `ServiceHost` instead of taking
  explicit constructor dependencies
- a handwritten asset manifest
- a scene field storing runtime handles without a path/string serialization
  story
- a low-level `core/` module including `app/`, `graphics/`, `render/`, or
  gameplay-specific headers, except for the known `core/assets` integration
  layer

## Existing Documentation

Read this map with:

- `docs/ecs/overview.md` for ECS concepts and new-component basics
- `docs/ecs/queries.md` for query accessors
- `docs/ecs/command-buffers.md` for deferred structural mutation
- `docs/ecs/component-traits.md` for lifecycle hooks
- `docs/ecs/parallelization.md` for job/async rationale and constraints
- `docs/assets/pipeline.md` for the asset pipeline plan and current status
- `docs/audio/runtime.md` for the audio source component slice
