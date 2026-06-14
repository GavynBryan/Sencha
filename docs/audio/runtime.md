# Sencha Audio Runtime: Plan — Slice 1, scene audio sources

Status: **working plan** (branch `asset-pipelines`, 2026-06-12). This is the
audio runtime plan that `docs/assets/pipeline.md` Decision F pointed at, begun
with its first slice: a scene-authored audio source component. Decisions are
tagged the usual way: **Settled** / **Proposed** / **Open**.

The product frame, restated because it grades everything here: room-scale
zones, 2–4 live, constant backtracking churn, dormant preloaded neighbors.
Audio content at this scale is ambient loops per room, placed one-shot
stingers, and gameplay SFX. The flagship requirements are therefore that
**a dormant preloaded room is silent**, that **zone churn never leaks a voice
or dangles a clip reference**, and that authoring a room's soundscape is
data in the scene file, not code.

---

## What exists today (inventory, 2026-06-12)

More than expected — the engine pre-built the audio lane and never used it:

| Piece | State |
| ----- | ----- |
| `AudioService` | Complete v1 backend: buses with voice budgets and steal policies (`Reject`/`StealOldest`), gain/pan/**looping**, pause/resume, generational `VoiceId`s, `Tick()` retires drained voices. **Constructed by nobody in the engine** — AudioTest builds it by hand; it is not in `ServiceHost` and nothing calls `Tick`. |
| Clip assets (Stage 4d) | `.sclip` container, headless `AudioClipCache`, `AssetSystem::LoadAudioClip`/`TryAcquire`/`Release`, preload wave 1, WAV/OGG cook. Done. |
| The audio frame lane | `ZoneParticipation.Audio` → `FrameRegistryView.Audio` (dormant zones excluded by construction) → `AudioContext` + `EngineSchedule::RunAudio` + the `HasAudio` system concept, invoked in `FramePhase::Update` after `RunFrameUpdate`. **No system registers for it.** |
| Component machinery | `ComponentTraits` retain/release through World resources (the `StaticMeshComponentAssets` pattern), `TypeSchema` + `SceneFieldCodec` (handle codecs resolve through `AssetSystem`), `RegisterComponent<T>()`. A new component is a stamped template. |
| Manifests | `CollectAssetPaths` is schema-agnostic: the first component that serializes an audio ref is preloadable with **zero** new manifest code — and closes the Stage 4d gate gap (manifest-driven audio preload through a real zone). |

The genuinely new work is one component, one system, one service
registration, and the lifetime rules connecting them.

## Decisions

### A. Slice scope — scene-authored emitters only

**Settled** (product call, 2026-06-12).

`AudioSourceComponent` is a scene-resident *emitter*: ambient loops and
placed one-shots, authored in scene JSON, streamed with the zone. The two
neighboring surfaces stay where they are:

- **Imperative fire-and-forget SFX** (gameplay events) keeps calling
  `AudioService::Play` directly — that surface already exists and works. A
  trigger/event component belongs to a future gameplay-data slice, not here.
- **Streamed music** stays deferred to a later slice of this plan, exactly
  as pipeline Decision F drew the boundary: a long-lived decode source is a
  different contract from a resident clip.

Naming: "source," not "player." A *player* implies imperative playback
control; that control surface is `AudioService`. The component is a thing
that exists in a place and emits.

### B. The component — authored data, runtime fields, one new codec

**Proposed.**

```cpp
struct AudioSourceComponent
{
    // Authored (TypeSchema fields, serialized):
    AudioClipHandle Clip;          // scene JSON carries the asset path
    std::string Bus = "Sfx";       // bus name, resolved at play time
    float Gain = 1.0f;             // [0, 1]
    float Pan = 0.0f;              // [-1, +1], static — see Decision F
    bool Looping = false;
    bool PlayOnActive = true;      // emit whenever the zone is audio-active

    // Runtime (not in TypeSchema, default-initialized on load):
    VoiceId Voice;                 // generational; stale ids resolve to nothing
    bool Started = false;          // one-shots fire once per component lifetime
};
```

- `SceneFieldCodec<AudioClipHandle>` follows the mesh/material codecs:
  save writes the path (`AssetSystem::GetPathForAudioClip` — the parity gap
  deliberately skipped in 4d closes here), load resolves through
  `AssetSystem::LoadAudioClip`. Registration is one line in
  `EngineSceneComponents` (`world/ComponentManifest.h`).
- The bus is a **name**, resolved per play. Buses are game config fixed at
  `AudioService` construction, but the component cannot resolve an index at
  deserialization time (no service in scope) and per-play name lookup is
  what `AudioService` already does. Default `"Sfx"` over `"Engine"` because
  the built-in Engine bus (1 voice, Reject) is reserved for engine sounds —
  a game that defines no `Sfx` bus gets the warn-and-silent treatment
  (Decision E), not a stolen engine voice.
- Editor coordination: the component arrives as one more `TypeSchema` chunk
  in scene JSON; the editor branch rebases over this and round-trips it
  like any other component. Authoring UI for it is explicitly outside
  editor Phase 1 (its own non-goals list).

### C. Lifetime — one invariant, two owners

**Proposed.** The invariant everything serves, from the `AudioService::Play`
contract ("the clip must remain valid for the duration of playback"):

> **A voice never outlives the clip reference that feeds it.**

Division of labor, mirroring the render components:

- **Traits own the asset edge.** A World resource
  `AudioSourceRuntime { AudioClipCache* Clips; AudioService* Audio; }`
  (the `StaticMeshComponentAssets` pattern; either pointer may be null in
  headless worlds). `OnAdd` retains the clip — and never plays: the zone
  may be dormant, and deserialization is not activation. `OnRemove` calls
  `Stop(Voice)` **and then** releases the clip, in that order, in the same
  hook — this is the invariant's enforcement point, and it covers both
  entity destruction and zone detach (teardown fires traits; the Stage 1
  release-chain tests are the precedent).
- **The system owns the activation edge** (Decision D), because dormancy
  transitions fire no component hook: a zone leaving the audio view keeps
  its components alive — only the view membership changes.

### D. `AudioSystem` — engine-registered, owns the tick, mark-and-sweep

**Proposed.** Engine-side system registered in `Engine::Initialize` the way
`DefaultRenderPipeline` is, running in the pre-built audio lane
(`FramePhase::Update`, presentation time — audio never touches the fixed
tick, so determinism is unaffected). Per frame, in order:

1. **`AudioService::Tick()` first** — retire drained voices so their slots
   are reusable by this frame's plays. This is the service's one tick site;
   if the service is missing or `!IsValid()` (no audio device, CI), the
   system no-ops — the llvmpipe/headless posture.
2. **Sweep**: the system keeps a table mapping each `Registry*` it has seen
   active to the `VoiceId`s it started there. Registries absent from this
   frame's `Registries.Audio` view get every voice in their entry `Stop`ped
   — by id copy only, never dereferencing the registry pointer (a detached
   zone's registry is gone, but its voices were already stopped by
   `OnRemove`, and generational ids make double-stop a no-op; the pointer is
   just a table key, so even a since-reused address is harmless).
3. **Visit** `AudioSourceComponent`s in the active view and apply Decision
   E's start rules, refreshing the table. Dormant preloaded rooms are
   silent *by construction* — they are simply not in the view.

### E. Start/stop semantics — written down so they aren't discovered

**Proposed.**

| Case | Behavior |
| ---- | -------- |
| `PlayOnActive` + `Looping` | Playing whenever its zone is in the audio view. Stops on leaving the view (sweep), starts fresh on re-entry. Resume-from-position is deferred until something needs it — room transitions refade in practice. |
| `PlayOnActive` + one-shot | Fires once per component lifetime (`Started`). Zone re-entry does **not** replay. Per-activation replay is the recorded alternative, deferred until content asks. |
| `!PlayOnActive` | Inert data. Gameplay flips `PlayOnActive`/`Started` and the system reacts next frame — event-free triggering that costs nothing to build. |
| Unknown bus | Warn once per source, stay silent — the Decision L `alpha_mode: blend` pattern: content keeps authoring, the warning is the to-do list. |
| Voice stolen | A looping source whose voice vanished restarts on the next visit. Under `StealOldest` this can thrash; the authoring rule is **looping ambients belong on `Reject` buses with headroom** (room-scale needs a handful). If guidance proves insufficient, the fix is a "retired-by-steal" query on `AudioService` — deferred. |

### F. Spatialization — deliberately absent, additively reachable

**Settled** scope. `Pan` is static authored data; there is no listener and
no attenuation in this slice. A future spatial slice adds optional fields
with neutral defaults (the Decision L schema discipline — authored content
never churns). The deciding inputs are named now so that slice is
mechanical: listener = camera vs. player entity; 2D pan + distance falloff
vs. a real 3D model (genre says the former); occlusion almost certainly
never. What this slice must not do is bake "non-spatial" into the system's
shape — and it doesn't: spatial is one more step between "visit" and
"play" that rewrites gain/pan per frame.

### G. `AudioService` joins the engine

**Proposed.** `Engine::Initialize` constructs `AudioService` from
`Configuration.Audio` (the config struct has waited for exactly this) into
`ServiceHost`. An invalid service (no device) is non-fatal: the engine
runs, the system no-ops. AudioTest keeps its hand-built setup — it tests
the service in isolation, which stays valuable.

## Slice 1 rollout — one stage, gated

Build order: codec + `GetPathForAudioClip` → component + traits →
`AudioService` into `Engine` → `AudioSystem` → CubeDemo content + bus
config → tests alongside each piece.

**Gate:**

- CubeDemo's scene places a looping ambient source and a one-shot; the
  manifest preloads their clips through the async lane — closing the Stage
  4d gate gap (manifest-driven audio preload through a real zone) — and
  both behave per Decision E under llvmpipe + dummy audio driver.
- Zone detach while a loop is playing is leak-free: stop-before-release is
  asserted in a headless test (real `AudioClipCache`, `AudioService` on
  SDL's dummy driver; skip-if-unavailable, the Blender-test precedent).
- Dormancy round trip pinned headless: activate → loop starts; dormant →
  sweep stops it; reactivate → starts fresh. One-shot does not replay.
- Scene JSON round-trips the component (codec test); full suite green
  including TSan.

## Slice 1 status (2026-06-12)

Landed, test-verified (805 tests green; 6 new in
`test/runtime/AudioRuntimeTests.cpp`, TSan-clean). The slice is built; one
gate clause — the *graphical* CubeDemo run — is owed to an environment with
a display (below).

- `core/text/InlineString.h` — a fixed-capacity, trivially-copyable string,
  written because the ECS surfaced a constraint the plan missed: archetype
  storage relocates components with `memcpy`, so a component member must be
  trivially copyable and `std::string` is not. `AudioSourceComponent::Bus`
  is a `BusName` (`InlineString<32>`); a `static_assert` pins the component
  trivially copyable so the constraint can't silently regress. Reusable —
  Decision O's prefab tags and data-table keys will want it.
- `audio/AudioSourceComponent.h` — the component (Decision B), its
  `ComponentTraits` (Decision C: `OnAdd` retains the clip and never plays;
  `OnRemove` stops the voice **then** releases the clip), `TypeSchema`, and
  the `AudioSourceRuntime` World resource carrying `{AudioClipCache*,
  AudioService*}`. `SceneFieldCodec<AudioClipHandle>` resolves through the
  asset front door (`GetPathForAudioClip` closes the 4d parity gap), and a
  templated `SceneFieldCodec<InlineString<N>>` persists bus names as plain
  strings in both text and binary.
- `audio/AudioSystem.{h,cpp}` — the engine-registered system (Decision D):
  `Update(AudioService*, active registries)` is engine-free for headless
  tests; the `Audio(AudioContext&)` hook resolves the service from the
  ServiceHost and feeds it the audio view. Tick → sweep (by VoiceId copy,
  never dereferencing a registry pointer) → visit with the Decision E start
  rules. `Engine::Initialize` constructs `AudioService` from
  `Configuration.Audio` and registers `AudioSystem`; both wire into the
  pre-built audio lane that existed unused (`ZoneParticipation.Audio` →
  `FrameRegistryView.Audio` → `RunAudio`).
- `DefaultZoneBuilder` threads the audio cache + service into the zone the
  way it already threads mesh/material caches, installing
  `AudioSourceRuntime`; dormant zones are excluded from the audio view by
  construction, so a preloaded neighbor is silent for free.
- CubeDemo: an `Sfx` bus (8 voices, Reject — the loop-on-Reject authoring
  rule), `assets/audio/ambient.wav`, a looping ambient on the floor and a
  one-shot on the red cube, the `AudioClipImporter` in its cook registry,
  and `.Audio = true` participation. The manifest auto-includes
  `asset://audio/ambient.wav` — the schema-agnostic `CollectAssetPaths`
  walk needed no change, which is the Decision D/O claim paying out.
- Gate honesty:
  - Codec round-trip, lifetime stop-before-release-of-the-sole-reference,
    dormancy sweep + loop restart + one-shot no-replay, and the null-service
    no-op are pinned headless; the lifetime/sweep tests run a real
    `AudioService` on SDL's dummy driver (skip-if-unavailable, the
    Blender-test precedent), and were green here.
  - AudioTest already proves the front-door cook → load → play chain
    headless (Stage 4d), re-verified unchanged.
  - **Owed:** the in-app CubeDemo audio run (looping ambient audible, both
    sources behaving under the live frame loop) needs a display — this
    headless WSL box has no X server and the Vulkan instance doesn't enable
    `VK_EXT_headless_surface`, so the renderer can't start and `OnStart`
    never runs. `AudioService` initializing its two buses confirms the
    config path; the component-and-system behavior is covered by the unit
    tests with a real service. The graphical observation is owed on a
    machine with a display, same posture as the 4d wall-clock measurement
    owed until multi-room content exists.

## Deferred, with triggers

- **Spatial slice** (Decision F here): first game content needing
  positional audio.
- **Streamed music** (pipeline Decision F): the next slice of this plan;
  needs a decode-source contract, not a resident clip.
- **Trigger/event components**: the gameplay-data plan.
- **Per-activation one-shot replay** (Decision E): content request.
- **Steal-aware restart / retired-by-steal query** (Decision E): the
  Reject-bus authoring rule failing in practice.
- **Pause/resume on dormancy** (Decision E): a real resume-from-position
  need.
- **Mixer features** (snapshots, ducking, per-bus effects): no customer;
  not before a game ships sound design that wants them.
