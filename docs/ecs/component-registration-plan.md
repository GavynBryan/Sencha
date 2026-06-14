# Component Registration: Manifest Plan

Status: **implemented** (2026-06-12, branch `asset-pipelines`). All five steps
landed; full suite green (807 tests). Two deviations from the original plan
are recorded inline below, marked **Deviation**.
Branch context: coordinate with Editor Phase 1 (it consumes
`GetComponentSerializerEntries()` — that entry point did not change).

## Problem

Adding a serializable component today touches five files:

| Touchpoint | File | Cost |
| --- | --- | --- |
| Struct, `ComponentTraits`, `TypeSchema` | the component's own header | fine — co-located, carries real information |
| Trivially-copyable `static_assert` | the component's own header | ceremony, should be automatic |
| FourCC chunk ID | `world/serialization/SceneFormat.h` | per-type fact living three directories from the type |
| `ComponentStorageTraits<T>` specialization | `world/serialization/ComponentStorageTraits.h` | ~18 lines; 4 of 5 are identical boilerplate; drags `audio/` and `render/` includes into `world/serialization` |
| `RegisterComponent<T>()` line | `SceneSerializer.cpp` `InitSceneSerializer()` | forget it → component **silently never serializes** |
| `RegisterComponent<T>()` line | `DefaultZoneBuilder.cpp` `PrepareRegistry()` | forget it → loads still work, but programmatic `AddComponent` before the first load hits the registration-after-entity-creation assert (debug) / UB (release), order-dependent |

Additional silent failure: `RegisterComponentSerializer` skips any serializer
whose chunk ID **or** JSON key collides with an existing entry — no assert, no
log. A genuine collision is indistinguishable from idempotent re-registration.

Target end state: **adding a component = write its header + add one line to
one manifest.** Everything about a component lives in its header; the set of
scene components is named in exactly one place.

## Non-Goals / Invariants

- **Wire format does not change.** Same FourCCs, same chunk layout, same JSON
  keys. Existing `.scene` files load unmodified. (This is what makes the
  refactor safe relative to the editor branch.)
- `GetComponentSerializerEntries()` keeps its signature and semantics.
- `World::RegisterComponent<T>` keeps its register-before-first-entity rule.
- No static-initializer self-registration. The engine is a static library;
  registrar objects in unreferenced TUs get dead-stripped, init order is
  nondeterministic, and the component set becomes invisible. An explicit
  manifest is the legible choice.
- No build-time codegen (UHT-style). Second toolchain stage, hurts legibility.

## Steps

### 1. Move scene facts into `TypeSchema<T>`

Add the chunk ID next to the JSON name, in the component's own header:

```cpp
template <>
struct TypeSchema<AudioCaptionComponent>
{
    static constexpr std::string_view Name = "AudioCaption";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('A', 'C', 'A', 'P');
    static auto Fields() { ... }
};
```

`SceneFormat.h` keeps `SceneMagic`, `SceneVersion`, `MakeFourCC`, and the
non-component chunks (`Registry`, `Hierarchy`). The per-component constants in
`SceneChunk` are deleted; the FourCC values themselves are unchanged.

### 2. Make `ComponentStorageTraits` a working primary template

The primary template implements the standard pattern every component but
`LocalTransform` already uses:

- `BinaryChunkId = TypeSchema<T>::SceneChunkId`
- `Register`: idempotent `registry.Components.RegisterComponent<T>()`
- `Add`: reject duplicates, then `AddComponent`

Delete the `StaticMeshComponent`, `CameraComponent`, `AudioSourceComponent`,
and `AudioCaptionComponent` specializations. Keep `LocalTransform`'s (it
co-registers `WorldTransform` + `Parent` and mirrors `LocalTransform` into
`WorldTransform` on add).

**Deviation:** the plan originally said to move the `LocalTransform`
specialization next to the transform components. It stayed in
`ComponentStorageTraits.h` instead: the primary template compiles for any
component with a `TypeSchema`, so a specialization living in a header that
some translation unit fails to include would silently bind the default
behavior there — an ODR violation. All storage-traits specializations live in
one header, by rule (documented in the header itself). The layering win
survives: `ComponentStorageTraits.h` includes only `world/transform`, no
`audio/` or `render/`.

### 3. Create the manifest

New header `engine/include/world/ComponentManifest.h`:

```cpp
// The single authoritative list of serializable scene components.
// Adding a component to the engine means adding it here — nowhere else.
using EngineSceneComponents = std::tuple<
    LocalTransform,
    CameraComponent,
    StaticMeshComponent,
    AudioSourceComponent,
    AudioCaptionComponent>;

template <typename Fn>
void ForEachSceneComponent(Fn&& fn);   // fold over the tuple's types
```

Consumers:

- `InitSceneSerializer()` folds over the list calling `RegisterComponent<T>()`.
- `DefaultZoneBuilder::PrepareRegistry` folds over the list calling
  `ComponentStorageTraits<T>::Register(registry)`. Using the traits (not raw
  `RegisterComponent`) is what keeps `WorldTransform`/`Parent` registered via
  `LocalTransform`, preserving today's `PrepareRegistry` behavior exactly.

The two lists that could previously drift apart are now the same list, which
removes the order-dependent registration bug class outright.

Games extend, not edit: game-specific components keep using
`RegisterComponent<T>()` after `InitSceneSerializer()` and registering storage
in their own zone-builder hook. A game-side manifest helper is a follow-up if
a real game needs it; don't build it speculatively.

### 4. Safety nets

- `World::RegisterComponent<T>` gains
  `static_assert(std::is_trivially_copyable_v<T>)` — archetype chunks relocate
  with `memcpy`, so this is a structural requirement, enforced once. Delete
  the per-header asserts (e.g. `AudioCaptionComponent.h`).
- `RegisterComponentSerializer` distinguishes the two collision cases:
  - chunk ID **and** JSON key both match an existing entry → idempotent
    re-registration, skip silently (keeps `InitSceneSerializer()` re-entrant);
  - only one matches → genuine collision: assert in debug, log error in
    release, refuse to register.

### 5. Tests

- Manifest uniqueness: every entry in `EngineSceneComponents` has a unique
  `SceneChunkId` and a unique `TypeSchema::Name` (compile-time if convenient,
  runtime test otherwise).
- Round-trip parity: a scene saved before this change loads identically after
  it (golden binary fixture, or save/load/compare within the test).
- Defaults discipline (also the C++26 enabler, see below): for every field
  with a `DefaultValue`, assert `T{}.*field.Ptr == *field.DefaultValue` via
  `SchemaVisit`. Schema defaults must equal member initializers.
- Update tests that hand-register the deleted trait specializations, if any.

## C++26 Readiness

Reflection (P2996, in C++26; Clang support still experimental as of mid-2026)
will eventually let a generic `TypeSchema` primary template synthesize
`Fields()` from the struct itself. The manifest design keeps that a drop-in:

- **Registration topology is reflection-proof.** C++26 cannot enumerate "all
  structs with property X" across a program; a manifest is needed regardless.
  Nothing in steps 1–4 gets thrown away.
- **The serializer only consumes `TypeSchema<T>::Fields()`.** When reflection
  lands, hand-written `TypeSchema` specializations become overrides of a
  reflection-backed primary template; serializer, codecs, manifest, and traits
  are untouched.
- **Two conventions to hold the line on now,** so the generated schemas match
  the wire format:
  1. JSON keys are `snake_case` of the `PascalCase` member name
     (`DurationSeconds` → `duration_seconds`). The reflection path will derive
     keys with a constexpr Pascal→snake transform, so new components must not
     drift from this convention (the existing five already conform).
  2. Field defaults equal member initializers, enforced by the test in step 5.
     The reflection path reads defaults off `T{}`; any schema default that
     disagrees with the member initializer would silently change on migration.
     The test makes that impossible.

## Order of Work

1. Step 1 + Step 2 (chunk IDs into `TypeSchema`, default traits) — compiles
   green, behavior identical.
2. Step 3 (manifest + rewire the two consumers) — delete the per-site lists.
3. Step 4 (asserts + collision handling).
4. Step 5 (tests), run full suite, verify CubeDemo round-trips its scene.

## Acceptance

- Adding a hypothetical `FooComponent`: one new header + one manifest line,
  zero edits elsewhere. Forgetting the manifest line is the only possible
  omission, and it fails loudly (component never appears anywhere, rather than
  half-working order-dependently). **Met.**
- `world/serialization` headers include no `audio/` or `render/` headers.
  **Met for `ComponentStorageTraits.h`** (the per-component axis this plan
  targeted). **Deviation:** `SceneFieldCodec.h` still includes `audio/` and
  `render/` headers for the per-*field-type* codec declarations
  (`AudioClipHandle`, `CaptionKind`, …). Scattering those next to their types
  would reintroduce the same silent-primary-template ODR hazard the storage
  traits avoid by living in one header, so `SceneFieldCodec.h` is the
  sanctioned single home for field codecs. Field-type codecs are a one-time
  per-type cost, not a per-component cost.
- Existing scene files and the CubeDemo load byte-identically. **Met by
  construction** — every FourCC, JSON key, and chunk layout is value-identical
  to before; round-trip and golden-chunk tests pass.
