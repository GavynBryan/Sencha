# Pillar 1 — Reflection & Module-Stable Component Identity

**Status**: Working plan (2026-06-14). Foundational; lands before all dependent work.
**Owns Stages S0 (spike) and S1 (rewrite).**

> This is the change the product owner explicitly green-lit a ground-up rewrite for:
> *"If we need to nuke our reflection system and rewrite it from the ground up, THAT IS
> FINE."* It is the 80% that is brutal to retrofit. We do it first, in its final shape.

---

## 1. The problem, precisely

A component's runtime identity is established in `World` by **`typeid(T)`**:

```cpp
// engine/include/ecs/World.h
template <typename T> ComponentId RegisterComponent() {
    const std::type_index ti(typeid(T));          // ← identity key
    auto it = TypeToId.find(ti);
    if (it != TypeToId.end()) return it->second;
    const ComponentId id = NextComponentId++;      // per-World, registration-ordered
    TypeToId[ti] = id;
    ...
}
template <typename T> ComponentId GetComponentId() const {
    const std::type_index ti(typeid(T));           // ← same key, every lookup
    auto it = TypeToId.find(ti);
    assert(it != TypeToId.end() && "Component type not registered");
    return it->second;
}
```

The **per-`World` `ComponentId`** (a `uint16_t` assigned by `NextComponentId++`,
[ComponentId.h](../../engine/include/ecs/ComponentId.h)) is *not* the problem — it is
already per-instance and resolved at registration. **The lookup key, `std::type_index(typeid(T))`,
is the problem.** `Resources` and `LegacyStores` use the same key
([World.h:444,484](../../engine/include/ecs/World.h)).

### Why `typeid` breaks across a module boundary

When a **game module** (`game.so`/`game.dll`) is loaded by a host (`app`, `editor`) that
also loaded `engine`, code in the game module instantiates engine templates against
*engine* types. Example — a game system:

```cpp
// in game.so
world.Query<LocalTransform, PlayerController>();   // LocalTransform is an engine type
```

`Query<LocalTransform, …>` is instantiated **inside game.so**, so it calls
`GetComponentId<LocalTransform>()` using **game.so's `typeid(LocalTransform)`**. But the
engine registered `LocalTransform` using **engine.so's `typeid(LocalTransform)`**. For the
lookup to hit, these two `std::type_index` values must compare equal across the module
boundary. Whether they do depends on:

- **RTTI symbol merging** by the dynamic linker — requires the `type_info` for
  `LocalTransform` to have **default visibility** and the linker to actually coalesce it.
  Build with `-fvisibility=hidden` (standard DLL hygiene, which we *want*) and the
  `type_info` symbols may not merge.
- **STL implementation details** — libstdc++ compares `type_info` by name string in some
  configs and by address in others (`__GXX_MERGED_TYPEINFO_NAMES`); libc++ and MSVC differ
  again. `std::type_index::hash_code()` hashes the name on libstdc++ but this is not a
  portable guarantee we want to bet the engine on.
- **`-Bsymbolic` / two-level namespaces / RTLD flags** — each can independently defeat
  merging.

This is a **platform-, flag-, and toolchain-dependent landmine.** For a commercial
multi-platform engine it is unacceptable to have component lookup silently miss because a
build flag changed. We remove the dependency entirely.

### What already works (do not over-rewrite)

The serializer path is **already boundary-clean by construction**, and the rewrite must
preserve that property:

- `ComponentSerializer<PlayerController>` is instantiated **in game.so**. Its vtable
  (`IComponentSerializer*`) crosses the boundary; the template body stays home. Every
  `RegisterComponent<T>()`, `TryGet<T>()`, `AddComponent<T>()` it calls is instantiated in
  game.so, using **one consistent `typeid`** — game.so's. So *game-owned* components never
  cross a `typeid` boundary today.
- The break is specifically **engine types referenced from game code** (the `Query` example)
  and any place a type is instantiated in *both* modules and assumed to match.

So the rewrite's job: make component identity **a value that is identical in every module
by construction**, independent of `typeid`, while keeping the vtable-based serializer
seam that already works. This is **component contract identity**, not a new universal
reflection framework.

---

## 2. The design: explicit `ComponentTypeId`

**Every component type carries an explicit, stable, module-independent identity**, declared
once at the type's definition and baked identically into any module that includes its
header. The `World` keys its `TypeToId` map on **that**, never on `typeid`.

This identity is a stable **component contract key**. If two modules declare the same
stable component name, they are claiming the same component contract. The engine validates
obvious mismatches loudly, but it does not try to infer deep C++ type equivalence at runtime.

### 2.1 The identity value

```cpp
// engine/include/ecs/ComponentTypeId.h  (new)
#pragma once
#include <core/identity/StrongId.h>

#include <cstdint>
#include <string_view>

// A stable, content-addressed component contract identity. Derived from a stable name that
// the component declares once. Identical in every module that compiles the same name — no
// RTTI, no link-time symbol merging, no per-build drift.
using ComponentTypeId = StrongId<struct ComponentTypeIdTag, std::uint64_t>;

// FNV-1a (or xxhash-style) over the name, constexpr so it folds at compile time and is
// usable in switch/if-constexpr. Collisions are a registration-time error (see registry).
constexpr ComponentTypeId MakeComponentTypeId(std::string_view name)
{
    std::uint64_t h = 1469598103934665603ull;
    for (char c : name) { h ^= static_cast<std::uint8_t>(c); h *= 1099511628211ull; }
    if (h == 0) h = 1; // zero is StrongId's invalid sentinel
    return ComponentTypeId{ h };
}
```

> **Why a name-hash and not a hand-assigned integer?** Hand-assigned integers across
> independently-developed game modules collide and require a central registry no third party
> can edit. A name-hash is decentralized: a game names its component `"acme.grapple_hook"`,
> the engine names `"sencha.local_transform"`, and neither can collide without a literal name
> collision, which we detect and reject at registration. Names also already exist for
> serializable components: `TypeSchema<T>::Name` is the JSON key. For those components we
> can use the schema name as the stable component name (§2.3), but that means it is no
> longer a casual display label. It is a wire/runtime contract key.

> **Why 64-bit, not the existing FourCC?** `BinaryChunkId` (FourCC, 32-bit) and the JSON
> `Name` already exist as identity-ish fields. FourCC's 4-char space collides far too easily
> across third-party modules. We keep FourCC for the **binary scene chunk tag** (it is a
> file-format concern) but base *runtime type identity* on the 64-bit name hash. §2.4
> reconciles the three.

### 2.2 Binding a type to its id

A single trait, specialized once per component, next to the type (mirrors how
`TypeSchema<T>` and `ComponentTraits<T>` already work):

```cpp
// the binding the engine and every game use identically
template <typename T> struct ComponentTypeKey;   // primary left undefined

// Specialization helper macro (optional sugar) — but we derive it from TypeSchema instead:
```

We do **not** want a third hand-written specialization per serializable component. Instead
we **derive** the key from `TypeSchema<T>::Name`, which every serializable component already
declares:

```cpp
// engine/include/ecs/ComponentTypeId.h
template <typename T>
concept HasComponentName = requires { TypeSchema<T>::Name; };

template <typename T> requires HasComponentName<T>
constexpr ComponentTypeId ComponentTypeIdOf()
{
    return MakeComponentTypeId(TypeSchema<T>::Name);
}
```

Result: declaring `TypeSchema<PlayerController>::Name = "acme.player_controller"` is the
*only* thing a programmer writes for a serializable component, and it simultaneously gives
the JSON key **and** the module-stable runtime identity. One source of truth, but with a
clear contract: this name is stable and namespaced, not UI copy.

> Components with no `TypeSchema` (pure-runtime, never-serialized, engine-internal tags like
> `WorldTransform`, `Parent`, and many test-only components) still need identity. For those
> we keep a fallback: a tiny `SENCHA_DECLARE_COMPONENT_TYPE(Type, "name")` macro that
> specializes a `ComponentTypeKey<T>` with an explicit stable name. The `World` resolves
> identity as: `ComponentTypeKey<T>` if specialized, else `ComponentTypeIdOf<T>()` from the
> schema. At least one source must exist; the explicit key is allowed to override schema
> identity for migration or runtime-only components. **`typeid` is gone from the lookup
> path.**

### 2.3 Stable-name policy

Stable component names are lower-case dotted identifiers:

- `sencha.local_transform`
- `sencha.world_transform`
- `sencha.parent`
- `editor.brush`
- `leveldemo.player_controller`
- `vendor.package.component`

Existing serialized JSON keys may need compatibility handling during migration. New
runtime identities must be namespaced; do not introduce new unqualified names like
`"Transform"` or `"Camera"` as stable component identities.

### 2.4 Reconciling the three identity fields

After this change a serializable component has, intentionally, three facets:

| Facet | Type | Source | Used for |
|-------|------|--------|----------|
| Runtime identity | `ComponentTypeId` (u64) | hash of the stable component name | `World` type→id map key (replaces `typeid`) |
| JSON key | `std::string_view` | `TypeSchema<T>::Name` verbatim | scene JSON object key |
| Binary chunk tag | FourCC (u32) | `ComponentStorageTraits<T>::BinaryChunkId` | binary scene chunk header |

For current serializable components, the stable component name is normally
`TypeSchema<T>::Name`. If we must preserve old unqualified JSON keys while moving runtime
identity to namespaced keys, introduce that split explicitly (`StableName`/`JsonKey`) rather
than smuggling display/compatibility concerns into the runtime id.

The FourCC stays separate (it is a file-format detail with its own collision domain and
existing files), but it is **validated** against the serializer registry: two registered
components sharing a FourCC, a JSON key, or a `ComponentTypeId` in an incompatible tuple is a
hard error at registration time (§3.3). The plan keeps FourCC for now; a follow-up may retire
it in favor of the name hash in the binary format, but that is a format-version change, out
of scope here.

---

## 3. The `World` rewrite

### 3.1 New lookup key

`World::TypeToId` changes from `std::unordered_map<std::type_index, ComponentId>` to
`std::unordered_map<ComponentTypeId, ComponentId>` (hash on `.Value`). The template methods
change their key derivation only:

```cpp
template <typename T> ComponentId RegisterComponent() {
    static_assert(std::is_trivially_copyable_v<T>, "...");
    assert(!EntityCreated && "...");
    const ComponentTypeId key = ResolveComponentTypeId<T>();   // ← was typeid(T)
    auto it = TypeToId.find(key);
    if (it != TypeToId.end()) return it->second;
    // ... NextComponentId++, ComponentMeta as before, plus store `key` in the meta ...
}

template <typename T> ComponentId GetComponentId() const {
    const ComponentTypeId key = ResolveComponentTypeId<T>();   // ← was typeid(T)
    auto it = TypeToId.find(key);
    assert(it != TypeToId.end() && "Component type not registered");
    return it->second;
}
```

`ResolveComponentTypeId<T>()` = `ComponentTypeKey<T>::Id` if specialized, else
`ComponentTypeIdOf<T>()`. Everything downstream (`ComponentId`, `ComponentMeta`, archetype
signatures, chunks) is **unchanged** — they already key on the small integer, not the type.
This is the crucial containment: the rewrite touches *only* the type→id resolution, a
handful of template methods in one header.

### 3.2 Resources and legacy stores

`Resources` and `LegacyStores` ([World.h:439-533](../../engine/include/ecs/World.h)) also
key on `std::type_index(typeid(T))`. Audit each:

- **`Resources`** (`StaticMeshComponentAssets`, etc.) — engine-owned, instantiated and
  accessed **engine-side**. They do not currently cross a module boundary, but a game system
  could `GetResource<SomeEngineResource>()`. If this path needs cross-module lookup in the
  module spike, move resources to the same **minimal** stable-key mechanism via an explicit
  `ResourceTypeKey<T>`. Do **not** introduce `ResourceSchema` or resource reflection in this
  pass; resources need identity, not metadata.
- **`LegacyStores`** — marked legacy/migration-only. If still present, give it the same
  treatment or delete it if the archetype migration has retired its last caller (verify
  during S1). Do not port a `typeid` key forward.

### 3.3 Registration validation

`RegisterComponent` and the serializer registry gain conflict checks, because the whole
point is that independently-authored modules must fail loudly, never silently alias:

- On `World::RegisterComponent<T>`: store `ComponentTypeId`, stable name/debug name, size,
  alignment, and tag-ness in `ComponentMeta`. Re-registering the same key returns the
  existing `ComponentId` only if the observable storage contract matches. A same-key
  registration with a different name, size, alignment, or tag-ness is fatal. A same-key,
  same-layout registration is treated as the same declared component contract; the engine
  does not pretend it can prove deeper C++ type identity at runtime.
- In `RegisterComponentSerializer` ([SceneSerializer.cpp](../../engine/src/world/serialization/SceneSerializer.cpp)):
  add `virtual ComponentTypeId TypeId() const = 0` to `IComponentSerializer`. Registration
  compares the tuple `(TypeId(), JsonKey(), BinaryChunkId())`. It is idempotent only when all
  three match; if any field matches but the full tuple differs, reject the registration
  loudly. This catches copy/paste FourCC mistakes, JSON key reuse, and component-name
  collisions before a scene can be corrupted.

The contract is explicit: declaring the same stable component name means “this is the same
component contract.” If two modules lie about that and also match the storage metadata, the
engine cannot discover the lie without a broader reflection/fingerprinting system, which is
out of scope for this pillar.

---

## 4. Stage S0 — the boundary spike (do this before S1)

**Goal**: prove the scheme on the *real* DLL boundary before rewriting the whole engine on
top of it. This is the one place we de-risk experimentally rather than by reasoning.

Build a throwaway:

1. A minimal shared `engine` (or a cut-down test lib) exposing `World`, the new
   `ComponentTypeId` resolution, the serializer registry, and `LocalTransform`.
2. A throwaway `spike_game.so` that:
   - defines `struct GrappleHook { Vec3d Anchor; float Length; }` with
     `TypeSchema<GrappleHook>::Name = "spike.grapple_hook"`,
   - registers its serializer + storage through the engine ABI,
   - runs a system doing `world.Query<LocalTransform, GrappleHook>()` — i.e. references an
     **engine** component and a **game** component from game-module code.
3. A host exe that loads `engine` (shared) and `dlopen`s `spike_game.so`, builds a `World`,
   creates entities with both components, queries, and **scene-serializes to JSON and back**.

**Build the host and the game with `-fvisibility=hidden` and `-fvisibility-inlines-hidden`**
— i.e. the *hostile* configuration that would defeat `typeid` merging. The spike passes only
if it works *with* hidden visibility, because that is the shipping posture.

**Gate (S0):**
- `world.Query<LocalTransform, GrappleHook>()` from game-module code returns the right
  entities (proves engine-type identity resolves identically across the boundary).
- Scene save→load round-trips both components.
- A deliberately-induced identity conflict is rejected loudly: same stable name with
  different storage metadata, same JSON key with different `ComponentTypeId`, and same FourCC
  with different `ComponentTypeId`.
- Grep proves no `typeid`/`type_index` on the component-lookup path.
- **Decision recorded** in this doc's changelog: scheme confirmed, proceed to S1. If the
  spike surfaces a problem (e.g. a constexpr-hash ODR subtlety), it is fixed here, with one
  component, not after twenty.

> The spike is allowed to be ugly and is deleted after S0. Its value is the recorded result.

---

## 5. Stage S1 — the rewrite, across the engine

Once S0 is green:

1. Land `ComponentTypeId.h` and the `ResolveComponentTypeId<T>` machinery.
2. Convert `World`'s `TypeToId` (and Resources/LegacyStores per §3.2) to the stable key.
3. Add `ComponentTypeId` (and a debug name) to `ComponentMeta`; wire collision validation
   (§3.3).
4. Ensure every engine component in `ComponentManifest.h`
   ([engine/include/world/ComponentManifest.h](../../engine/include/world/ComponentManifest.h))
   resolves an id. Serializable components can use `TypeSchema::Name`; add the macro
   fallback for non-schema internals (`WorldTransform`, `Parent`, tags) and for tests.
5. Extend `IComponentSerializer` with `TypeId()` and update the serializer registry conflict
   checks.
6. Delete `typeid`/`std::type_index` from the component/resource lookup path. Leave `typeid`
   only where it is genuinely module-local and harmless (none should remain on these paths).

**Gate (S1):**
- Full test suite green (currently ~782; add the ones below). Behavior of CubeDemo,
  zone load, transform propagation, serialization all unchanged.
- New tests in `test/core/`:
  - `ComponentTypeIdTests.cpp` — constexpr hash stability; same stable name → same id;
    different stable names → different ids; `ResolveComponentTypeId` picks key-over-schema
    correctly.
  - `WorldStableIdentityTests.cpp` — register/lookup/add/query a component identified only by
    stable name; storage-contract conflict detection fires; round-trip through archetype
    moves.
  - `SceneSerializerIdentityTests.cpp` — serializer idempotency requires matching
    `(TypeId, JsonKey, BinaryChunkId)`; partial matches are rejected.
  - A **two-translation-unit** test that registers a component in TU A and looks it up in
    TU B (the in-process analog of the cross-module case) to pin that identity does not
    depend on a single instantiation site.
- The S0 spike's positive assertions are reproduced as a **kept** integration test under the
  real module loader once S2 exists (forward-linked from 02-).

---

## 6. Impact on existing code (inventory)

Files that must change (non-exhaustive; verify during S1):

- `engine/include/ecs/World.h` — the lookup methods, `ComponentMeta`, Resources/Legacy.
- `engine/include/ecs/ComponentId.h` — unchanged (the small-int id stays).
- **new** `engine/include/ecs/ComponentTypeId.h`.
- `engine/include/world/serialization/IComponentSerializer.h` — add `TypeId()`.
- `engine/include/world/serialization/ComponentSerializer.h` — implement `TypeId()`.
- `engine/include/world/serialization/SceneSerializer.h` / `.cpp` — conflict checks.
- `engine/include/core/metadata/TypeSchema.h` — document that `Name` is a stable key when
  used for component identity; add `StableName`/`JsonKey` only if migration requires a split.
- `engine/include/ecs/ComponentTraits.h` — no change (lifecycle hooks are template-resolved,
  module-local where used).
- Any call site doing `typeid` on a component/resource type — grep `engine/` and fix.

Things that **do not** change: archetype storage, chunks, signatures, queries' iteration,
`ComponentStorageTraits`'s shape, and the scene JSON/binary formats. The serializer
interface gets one identity accessor; the behavioral blast radius is deliberately the
type→id resolver and its validators.

---

## 7. Non-goals

This pillar is intentionally narrow. It does **not** introduce:

- A general reflection registry.
- Property/editor metadata expansion.
- Resource schemas or resource reflection unless cross-module lookup forces a minimal
  resource identity key.
- A binary scene format change.
- Global component id assignment. Per-`World` `ComponentId` remains the dense storage id.
- Runtime proof that two arbitrary C++ structs are semantically identical. The stable
  component name is the contract.

---

## 8. Risks & mitigations

- **Constexpr-hash ODR / fold consistency across modules.** Mitigated by S0 (built with
  hidden visibility, the hostile case) and the two-TU test. The hash is a pure constexpr
  function over a string literal — identical in every TU by the standard, with no external
  linkage to merge.
- **Hash collisions between names.** 64-bit FNV/xxhash over short distinct names: astronomically
  unlikely, and **detected at registration** when the colliding components disagree in name,
  storage metadata, or serializer tuple (§3.3). A literal same-name/same-layout declaration is
  treated as a claim to the same component contract. Document the `vendor.name` naming
  convention to make accidental collisions a non-issue socially too.
- **Performance.** `GetComponentId<T>()` was a map lookup on a `type_index`; it becomes a map
  lookup on a `u64`. Equal or faster (integer hash/compare vs. `type_info`). Hot query paths
  already cache `ComponentId`, not the type key, so no hot-path change.
- **Scope creep into a full reflection framework.** Resist. This is *identity*, not a
  property system. `TypeSchema`/`Field` stay exactly as they are; we only make identity
  module-stable. Anything beyond is a separate plan.

---

## 9. Changelog (decisions recorded here as they land)

- 2026-06-14 — Plan written. Scheme chosen: name-hash `ComponentTypeId` derived from a
  stable component name (normally `TypeSchema::Name` for serializable components), replacing
  `typeid` as the `World` lookup key; FourCC retained for the binary chunk tag; collision
  detection at registration. S0 spike specified.
- 2026-06-14 — Review pass tightened scope: identity is a component contract key, not a
  general reflection system; serializer registration will include `ComponentTypeId`; resource
  work is limited to minimal stable keys if needed; non-goals recorded.
- 2026-06-14 — **S0 spike GREEN. Scheme confirmed; proceeding to S1.** A throwaway
  `spike_game.so` (compiled `-fvisibility=hidden -fvisibility-inlines-hidden`, the
  hostile posture for `typeid` merging) was loaded by a host that links a shared
  `libspike_engine.so`. Verified across the real `.so` boundary, all green:
  - `ResolveComponentTypeId<LocalTransform>()` computed *inside* game.so is bit-identical
    to the host's value (the core claim — engine-type identity resolves the same in both
    modules with **no RTTI**).
  - A serializer registered by game.so is found by the host under the same name-hash key.
  - `world.Query<LocalTransform, GrappleHook>()` instantiated in game.so matched exactly
    the joined entities and read both engine- and game-component data correctly.
  - Scene save→load round-trips both components.
  - Conflicts rejected loudly: same name/diff layout, same JSON key/diff `ComponentTypeId`,
    same FourCC/diff `ComponentTypeId` (serializer registry); and same stable name/diff
    storage layout (World registration).
  - Grep confirms no `typeid`/`type_index` in code on the identity path.
  No constexpr-hash ODR subtlety surfaced. The throwaway lives under `spike/` (build +
  run via `spike/build.sh`); delete after S1 lands its kept tests. `ComponentTypeId.h`
  written during the spike is **permanent** — it is the real header S1 builds on.
- 2026-06-14 — **S1 rewrite landed. Full suite green: 867 tests (was 852), 0 failures.**
  - `World::TypeToId` now keys on `ComponentTypeId` via `ResolveComponentTypeId<T>()`;
    `ComponentMeta` carries `{TypeId, Name}`; `RegisterComponent` asserts a same-identity
    re-registration matches storage layout (size/align/tag) or aborts (§3.3).
    `GetComponentId`/`IsRegistered` resolve through the stable key. `typeid` is **gone
    from the component lookup path** (verified by grep).
  - Engine non-schema components got explicit identities: `WorldTransform` →
    `sencha.world_transform`, `Parent` → `sencha.parent`. `LocalTransform` identity
    derives from its existing `TypeSchema::Name` (`"Transform"`) — left unqualified to
    avoid breaking on-disk scenes; the `StableName`/`JsonKey` namespacing split (§2.4) is
    **deferred** (trigger: a deliberate scene-format migration pass).
  - `IComponentSerializer` gained `TypeId()`; the serializer registry now validates the
    full `(TypeId, JsonKey, BinaryChunkId)` tuple — full match is idempotent, any partial
    overlap aborts.
  - New kept tests (`test/core/`): `ComponentTypeIdTests`, `WorldStableIdentityTests`,
    `SceneSerializerIdentityTests`, and a two-TU test (`TwoTuIdentity*`) pinning that
    identity is independent of the instantiation site. Conflict paths covered by death tests.
  - **§3.2 decision — Resources/LegacyStores left on `typeid` for now.** They are
    engine-owned, in-process, single-module; nothing crosses a module boundary through
    them today, and the plan forbids introducing resource reflection speculatively. They
    are *not* on the component lookup path. Trigger to revisit: the S2 module spike needs a
    game system to `GetResource<EngineResource>()` across the boundary → introduce a minimal
    `ResourceTypeKey<T>` then. (LegacyStores is migration-only; retire with its last caller.)
  - The `spike/` tree (the S0 throwaway) was **deleted** once its assertions were
    reproduced as kept tests: `TwoTuIdentity*` for in-process identity, and — landing in
    S2 — `test/runtime/GameModuleLoaderTests.cpp` for the real-`.so` boundary on the
    production loader.
