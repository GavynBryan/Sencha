# Handle Convergence: Implementation Plan

Status: **complete** (branch `asset-pipelines`). All eight steps landed; the
full suite (850 tests) and the live llvmpipe texture/render gate are green, and
the deleted-symbol / packed-bit sweeps are empty. See §7 (Outcome) for what
actually shipped, including two deviations from the plan as written.

Originally a sequenced, incremental plan to collapse Sencha's three parallel
handle systems into one uniform model. Each step builds green and is
independently mergeable — no big-bang rewrite.

This is a *removal*, not a patch: it deletes the packed-handle layout, the
`AssetCache` dual-shape branch, the standalone `GenHandle`/`SlotRegistry`/
`GenRef` universe, the per-handle `TypeSchema`s, and seven hand-rolled handle
structs, replacing them with two generators, one ownership wrapper, and a
couple of pool templates.

---

## 1. The governing principle

**Handles are transient and never persisted; identities are persisted and
never resolved to memory.**

A `TextureHandle` is a runtime reference into a pool — meaningless across a
save or restart. An `AssetId`/path is a stable name — meaningless as a memory
reference. Today these blur (`StaticMeshHandle` carries a `TypeSchema`,
`SkinnedMeshHandle` doesn't, and scene data serializes handles as asset paths
anyway). The rule makes it law: **handles carry no reflection and never
serialize**; the persisted form is always the identity, resolved to a handle
at load (which `SceneFieldCodec` already does). One axis of variation
disappears.

---

## 2. The target model (four concepts, all generated)

### 2.1 `Handle<Tag>` — `core/handle/Handle.h` (new)

```cpp
// One generational slot reference. Phantom Tag gives a distinct, non-
// interchangeable type per resource. 8 bytes, 0 = null, ONE layout.
template <typename Tag>
struct Handle
{
    uint32_t Index = 0;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != 0 && Generation != 0; }
    [[nodiscard]] bool IsNull() const { return !IsValid(); }
    bool operator==(const Handle&) const = default;
};

// Per resource: one line, no boilerplate.
using TextureHandle = Handle<struct TextureHandleTag>;
```

**Layout decision — split, 32-bit generation. Decisive, not aesthetic.** The
current packed format (`AssetCache`'s 20-bit index / **12-bit generation**)
*wraps* (`if (gen > kAssetCacheMaxGeneration) gen = 1`). A single hot slot
recycled 4096 times reuses generation 1, and a stale handle from that slot's
first life silently aliases the new occupant — an ABA hazard latent in
exactly Sencha's target workload (room streaming with constant backtracking
churn, plus hot reload recycling slots). A 32-bit generation makes wraparound
unreachable. Handles are passed around, not stored by the million; 8 bytes is
free. The packed layout is deleted, not preserved.

### 2.2 `Owned<H>` — generalize `core/handle/LifetimeHandle.h` (rename)

`LifetimeHandle<Owner, Key>` already type-erases the owner to
`ILifetimeOwner*`; the `Owner` type param is redundant. Simplify to:

```cpp
// Move-only RAII refcount reference to a Handle<Tag>. Attach() on
// construction, Detach() (Release) on destruction, through the type-erased
// ILifetimeOwner. The single owning-vs-observing distinction:
//   FooHandle        -> observing (cheap, may go stale)
//   Owned<FooHandle> -> owning   (refcounted, releases on destruction)
template <typename H>
class Owned { /* ILifetimeOwner* + H, today's LifetimeHandle body */ };

using TextureRef = Owned<TextureHandle>;   // was TextureCacheHandle
```

Applied uniformly — every pool, including the Renderer (which deletes
`GenRef`).

### 2.3 `StrongId<Tag, Underlying>` — `core/identity/StrongId.h` (new)

```cpp
// One generator for stable identities. Distinct from Handle: a *name*, not a
// slot ref. This is where hashing/serialization live — NEVER on handles.
template <typename Tag, typename Underlying = uint64_t>
struct StrongId
{
    Underlying Value = 0;
    [[nodiscard]] bool IsValid() const { return Value != 0; }
    friend bool operator==(StrongId, StrongId) = default;
};

using AssetId = StrongId<struct AssetIdTag, uint64_t>;
using TypeId  = StrongId<struct TypeIdTag,  uint32_t>;
using SerializedEntityId = StrongId<struct SerializedEntityIdTag, uint32_t>;
```

Hex text form, `std::hash`, and `Serialize`/`Deserialize` become free
functions/traits over `StrongId`, replacing the per-id copies.

### 2.4 Pool templates — policy varies, the handle never does

- `SlotPool<T>` (`core/handle/SlotPool.h`, new) — plain generational slot
  table handing out `Handle<Tag>`; replaces `SlotRegistry`. The weak_ptr
  two-stage staleness `GenRef` provides becomes an *optional policy on the
  pool* (an observing pool with destruction safety), not a separate handle
  universe.
- `AssetCache<TDerived, Tag, TEntry>` (existing, modified) — the refcounted,
  path-deduped cache, now using `Handle<Tag>` with the packed `if constexpr`
  branch deleted. (Rename to `AssetPool` optional; keep `AssetCache` to reduce
  churn.)

Both hand out `Handle<Tag>`; the generation check is shared.

---

## 3. Inventory → target mapping

| Today | Count | Target |
|-------|-------|--------|
| `TextureHandle`, `SkeletonHandle`, `AnimationClipHandle`, `AudioClipHandle` (packed `Id`) | 4 | `Handle<Tag>` aliases (split layout) |
| `StaticMeshHandle`, `SkinnedMeshHandle`, `MaterialHandle` (split `Index`/`Generation`) | 3 | `Handle<Tag>` aliases (layout-identical) |
| `ImageHandle`, `BufferHandle`, `ShaderHandle` (graphics, packed `Id`) | 3 | `Handle<Tag>` aliases |
| `VoiceId`, `CaptionId` (audio runtime, packed `Id`) — *found during Step 8, not in the original inventory* | 2 | `Handle<Tag>` aliases |
| 7 × `using …CacheHandle = LifetimeHandle<Cache, Handle>` | 7 | `Owned<Handle>` aliases |
| `TypeSchema<StaticMeshHandle>`, `TypeSchema<MaterialHandle>` | 2 | **deleted** (transient-handle rule) |
| `GenHandle<T>` + `SlotRegistry` + `GenRef<T>` (Renderer only) | 1 system | `SlotPool` + `Owned<Handle>`; `GenHandle.h` **deleted** |
| `AssetId`, `TypeId`, `SerializedEntityId` hand-rolled | 3 | `StrongId<Tag>` aliases |
| `AssetCache` `if constexpr (requires { h.Id; })` dual-shape | — | **deleted** (one layout) |

---

## 4. Sequence (each step builds green, independently mergeable)

Order minimizes risk: introduce additively, migrate the layout-identical
families first, then the layout-changing ones, then the separate domains,
then delete.

- **Step 0 — Introduce the primitives (additive, no migration).** Add
  `Handle<Tag>`, `Owned<H>` (as a thin generalization of `LifetimeHandle`,
  with `LifetimeHandle` kept as a temporary alias), `StrongId<Tag>`,
  `SlotPool<T>`. Unit-test each in isolation (handle distinctness/validity,
  `Owned` attach/detach refcount via a fake `ILifetimeOwner`, `StrongId`
  hex/hash round-trip, `SlotPool` insert/resolve/stale). *Gate: new tests
  green; nothing else changed.*

- **Step 1 — Migrate the already-split asset handles** (`StaticMeshHandle`,
  `SkinnedMeshHandle`, `MaterialHandle`) to `Handle<Tag>`. Layout is identical
  (`Index`/`Generation`), so this is a near-pure type swap. Keep `SlotIndex()`
  callers working (`RenderQueue.cpp`) — provide it as a free function
  `SlotIndex(handle)` or fold the one call site to `.Index`. *Gate: full suite
  green; render path unchanged.*

- **Step 2 — Delete `TypeSchema` on handles + confirm the transient rule.**
  Verify `TypeSchema<StaticMeshHandle>`/`<MaterialHandle>` are not consumed by
  any live reflection path: `SceneFieldCodec<…Handle>` already serializes
  these as asset paths and *rejects* binary, so the schemas are vestigial.
  Delete them. *Gate: full suite green; scene save/load round-trip
  (`SceneSerializerTests`) unchanged.* (If a consumer is found, that component
  is serializing a transient handle — fix it to use the asset path first, per
  §1, then delete.)

- **Step 3 — Migrate the packed single-`Id` asset handles** (`TextureHandle`,
  `SkeletonHandle`, `AnimationClipHandle`, `AudioClipHandle`) to the split
  `Handle<Tag>`, and **delete the `AssetCache` packed branch**
  (`if constexpr (requires { h.Id; })` → split only; remove
  `kAssetCacheIndexBits`/`Mask`/`MaxGeneration` and `DecodeIndex/Generation`).
  This is the load-bearing step. Migrate every `.Id` access on these handles
  to `.Index`/`.Generation` (or accessors). Confirm no code packs/unpacks
  their bits. *Gate: full suite green; the hot-reload bindless swap still
  works (texture handle is one of these) — re-run the CubeDemo texture gate;
  audio/skeleton/clip cache round-trips (their tests) green.*

- **Step 4 — Migrate `Owned`.** Replace the 7 `using …CacheHandle =
  LifetimeHandle<Cache, Handle>` with `Owned<Handle>` aliases; drop the
  redundant owner type param; remove the temporary `LifetimeHandle` alias.
  *Gate: full suite green; the material→texture and clip/mesh→skeleton refcount
  chain tests (which exercise `Owned` lifetime) green.*

- **Step 5 — Migrate the graphics handles** (`ImageHandle`, `BufferHandle`,
  `ShaderHandle`) to `Handle<Tag>`. These key into `unordered_map`s
  (`BindlessKey` uses `image.Id`) and the deletion queue — update the hash/key
  to `{Index, Generation}`. *Gate: full suite green; CubeDemo renders + the
  texture hot-reload gate still clean (validation-silent).*

- **Step 6 — Migrate the Renderer off `GenHandle`/`SlotRegistry`/`GenRef`** to
  `SlotPool` + `Owned<Handle>` (with the observing/destruction-safety policy
  if the weak_ptr two-stage behavior is actually relied on — verify against
  `Renderer.h`'s 8 use sites). **Delete `core/handle/GenHandle.h`.** *Gate:
  full suite green; CubeDemo runs clean.*

- **Step 7 — Migrate identity types** (`AssetId`, `TypeId`,
  `SerializedEntityId`) to `StrongId<Tag>`; move hex/hash/serialize to the
  `StrongId` traits/free functions; delete the hand-rolled wrappers in
  `core/identity/Id.h` and the `AssetId` struct body in `core/assets/AssetId.h`
  (keep the `AssetIdToString`/`FromString` surface). *Gate: full suite green;
  `IdentitySerializationTests`, `AssetIdTests`, manifest/id-map round-trips
  green.*

- **Step 8 — Final sweep.** Delete any now-dead helpers, confirm no `LifetimeHandle`/
  `GenHandle`/`GenRef`/packed-bit references remain, update docs. *Gate: full
  suite green; `grep` for the deleted symbols is empty.*

---

## 5. Risks / verification (call these out while implementing)

1. **Generation-wrap removal (Step 3)** — the whole point. After deleting the
   packed format, confirm no remaining code assumes a 32-bit packed handle id
   (search `.Id` on the migrated handles, and any `<<`/`>>`/mask on them).
2. **`TypeSchema` vestigiality (Step 2)** — verify by building with the schema
   deleted and running the scene serialization suite; if something needs it,
   it's serializing a transient handle and must be fixed to use the identity.
3. **Bindless hash keys (Step 5)** — `VulkanDescriptorCache::BindlessKey`
   hashes `image.Id`; updating `ImageHandle` to split means rehashing on
   `{Index, Generation}`. The hot-reload `UpdateSampledImage` path must stay
   correct (re-run the live gate).
4. **`SlotIndex()` (Step 1)** — one render call site; keep it as a free
   function over `Handle<Tag>` rather than a per-handle method.
5. **Renderer weak_ptr semantics (Step 6)** — `GenRef`'s two-stage staleness
   (registry-destroyed → null) may or may not be load-bearing; verify the 8
   use sites before collapsing onto a plain `SlotPool` policy. If it is needed,
   `SlotPool` grows a destruction-safe observing policy rather than losing it.
6. **No serialized handle layout dependency** — confirm nothing writes a raw
   handle to disk/binary (the transient rule guarantees this, but verify: the
   only handle codecs are the `SceneFieldCodec` path-based ones).

---

## 6. What gets deleted (the surgical list)

- 7 hand-rolled asset/graphics handle structs → `Handle<Tag>` one-liners.
- The packed `Id` layout + `AssetCache` `kAssetCacheIndexBits` machinery and
  the `if constexpr (requires { h.Id; })` dual-shape dispatch.
- `TypeSchema<StaticMeshHandle>`, `TypeSchema<MaterialHandle>`.
- `core/handle/GenHandle.h` (`GenHandle` + `SlotRegistry` + `GenRef`).
- The redundant `Owner` type parameter on `LifetimeHandle` (→ `Owned<H>`).
- Hand-rolled `AssetId`/`TypeId`/`SerializedEntityId` wrappers → `StrongId`.

End state: one way to name a resource (`StrongId`), one to reference a live
one (`Handle`), one to own one (`Owned`), and pools that differ only in
policy. Learned once, read the same everywhere.

---

## 7. Outcome (what actually shipped)

All eight steps landed on `asset-pipelines`. Final state: 850/850 tests green,
the live llvmpipe render/texture-hot-reload gate clean (no validation output),
and the sweeps for `GenHandle`/`GenRef`/`SlotRegistry`/`FeatureRef`/
`LifetimeHandle`/`kAssetCacheIndexBits` and for any shift/mask packed-handle
arithmetic both empty.

Two deviations from the plan as written, both deliberate:

- **Step 6 needed no `SlotPool`.** The plan assumed the Renderer's
  `GenHandle`/`SlotRegistry`/`GenRef` system was load-bearing and would be
  replaced by a `SlotPool` + an observing/destruction-safety policy (Risk #5).
  Verification against the actual use sites showed the `GenRef<T>` returned by
  `Renderer::AddFeature` is **never consumed** — both call sites discard it, and
  the per-frame loop iterates raw `IRenderFeature*` phase buckets that never
  touch `GenRef`/`weak_ptr`. So `AddFeature` now returns a plain `T*`, the
  `FeatureRegistry` member is gone, and `GenHandle.h` was deleted outright. No
  `SlotPool.h` was created — it would have been an unused abstraction. (If a
  future feature needs a destruction-safe observing pool, `SlotPool` is the
  place to add it, but nothing needs it today.)

- **Step 8 found two un-inventoried packed handles and converged them.** The
  §3 inventory missed `VoiceId` (`audio/AudioVoice.h`, returned by
  `AudioService::Play`) and `CaptionId` (`audio/Caption.h`, returned by
  `CaptionRuntime::Begin*`) — both hand-rolled single-`uint32 Id` handles on the
  old packed layout, the exact anti-pattern this effort removes. Both are
  transient slot references, never persisted: `CaptionId` lives in
  `AudioCaptionComponent` as live runtime state and is deliberately outside that
  component's serialized `TypeSchema`. Both are now `Handle<Tag>` aliases.
  `VoiceId` was a pure type swap (the voice pool was already 1-based with
  generation ≥ 1). `CaptionRuntime`'s 0-based slot array keeps its documented
  "slot + 1 so a zero id stays invalid" convention, now expressed as
  `Handle.Index = slot + 1` in `MakeCaptionId`/`CaptionIndex` — no change to the
  Tick/snapshot hot paths. Leaving them would have failed this step's own gate
  ("no packed-bit references remain").
