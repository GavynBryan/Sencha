# Stage 6 — Asset Hot Reload: Implementation Plan

Status: **ready to implement** (branch `asset-pipelines`). This is a
self-contained execution plan for Stage 6 of `docs/assets/pipeline.md`
(Decision H). It assumes Stages 1–5 are landed (they are, at commit
`539116e`, 843 tests green). Hand this to the implementer; every load-bearing
fact below was verified against the current tree.

---

## 1. Goal and scope

**Goal (Decision H gate):** edit a source asset (e.g. a PNG) while CubeDemo is
running and see the change in the live game within a frame or so, with **zero
handle invalidation** (every live component keeps its handle) and a **clean
deletion queue** (no immediate destroy of a resource an in-flight frame may
still reference).

**In scope:** hot reload of **textures, static meshes, and materials**. These
are the three Decision H names.

**Out of scope (do not build):**
- Skinned meshes and audio clips — reuse the same machinery later; not gated
  here. (Skinned mesh is trivial to add once the static-mesh path works; audio
  needs an `AudioClipCache` swap path. Leave hooks, don't implement.)
- Scene/`.json` edits and any topology change (a *new* or *deleted* asset
  file) — "archetype-changing scene edits are editor territory" (Decision H).
  The watcher only reacts to **content edits of already-known source files**.
- Shipping builds — the entire feature is dev-only, gated on
  `SENCHA_ENABLE_COOK` (the watcher and re-cook never ride a release binary,
  the glslang/cook precedent).

**Non-negotiable invariants (the whole point of the stage):**
1. A reload **never changes a handle**. It swaps the *contents* of an existing
   cache slot, keeping the slot index, generation, refcount, and (for
   textures) the bindless descriptor index. Every `StaticMeshComponent`,
   `MaterialHandle`, etc. that was valid before the reload is valid after.
2. The **old GPU resource is retired through `VulkanDeletionQueueService`**,
   never destroyed inline. The deletion queue holds it `framesInFlight + 1`
   frames, so no in-flight command buffer can reference freed memory.
3. The feature is **purely additive**: with the watcher off (or the source
   unchanged), behavior is byte-identical to today.

---

## 2. What already exists (building blocks — verified)

| Piece | Where | Use in Stage 6 |
|-------|-------|----------------|
| Staged load split | `core/assets/AssetLoader.h` — `IAssetLoader::LoadStaged` (pure, task-thread) + `CommitTyped` (owner-thread) | The *decode* half of a reload reuses `LoadStaged` unchanged; the *swap* half is a new commit variant. |
| Cook / re-import | `assets/cook/ImportOnDemand.cpp` (`ImportAssetsOnDemand`), `assets/cook/AssetImporter.h` (`AssetImporterRegistry`, `IAssetImporter::Import`, `ICookOutputWriter`) | Re-cooking one changed source reuses the importer registry. Factor a single-source re-import out of the directory walk. |
| Cooked index | `assets/cook/CookedCache.h` — `CookedCacheIndex`, `CookedSourceEntry` (source-rel-path → source hash → **set of artifacts**) | Maps a changed source to the cooked artifact paths it produces, so the reloader knows which cache entries to swap. |
| Registry | `core/assets/AssetRegistry.h` — `AssetRecord{ Type, Path, FilePath, ContentHash, ... }`, `FindByPath`, `RegisterOrVerify` | The reloader updates `ContentHash` on re-cook and uses `Type` to pick the cache. |
| Generational slot caches | `core/assets/AssetCache.h` (CRTP base: `Resolve(handle)` checks index+generation; `FindRegisteredHandle(path)`) | The slot is the swap target. Keep index/generation/refcount; replace the payload. |
| GPU teardown is already deferred | `VulkanBufferService::Destroy` → `DeletionQueue->EnqueueBufferDestroy`; `VulkanImageService::Destroy` → deletion queue | **Crucial:** calling the existing `Destroy(oldHandle)` during a swap is already frames-in-flight safe. No new deferral code needed for buffers/images. |
| Deletion queue cadence | `VulkanDeletionQueueService` — retains `framesInFlight + 1` frames; `AdvanceFrame()` called in `VulkanFrameService::BeginFrame` after the in-flight fence wait | Already correct; the swap just enqueues. |
| Bindless image array | `VulkanDescriptorCache::RegisterSampledImage(image, sampler) → BindlessImageIndex`, `UnregisterSampledImage(index)` | **Gap:** there is no "rewrite the descriptor at an existing index" method. Texture reload needs one (see §4.1). |
| Async lane | `jobs/AsyncTaskQueue` — `Submit<TPayload>(work → TPayload, commit(TPayload))`, `PumpWork`, `DrainCompletions`; pattern in `core/assets/AssetPreloader.cpp` | The reload decode runs as a task; the swap runs at the drain point, owner-thread. Mirror the preloader. |
| Per-frame loop | `runtime/RuntimeFrameLoop.h`, `app/Engine.cpp` | Where to tick the watcher poll and (already) advance the deletion queue. |
| Demo wiring precedent | `example/CubeDemo/CubeDemoGame.cpp` (already builds `AssetImporterRegistry` + calls `ImportAssetsOnDemand` under `SENCHA_ENABLE_COOK`) | Where to instantiate + tick the watcher. CubeDemo references `assets/textures/dev/checker.png` via `red.smat` — the gate target. |

---

## 3. Two kinds of watched file

The watcher must understand that not every asset has a cook step:

- **Cook sources** — `.png`, `.glb`, `.gltf`, `.blend`, `.wav`, `.ogg`. A
  change re-runs the importer (→ new `.stex`/`.smesh`/… in `.cooked/`), then
  reloads the resident cooked artifacts the source produces.
- **Authored runtime formats** — `.smat` (material JSON is loaded *directly*
  by `MaterialLoader`; there is no `.smat` importer). A change reloads the
  asset directly from the edited file — no cook step.

Both funnel into the same final action: *produce a fresh staged payload via
the asset type's loader, then swap it into the existing cache slot.* The only
difference is whether a re-cook precedes the `LoadStaged`.

(Scenes are authored JSON too, but they are out of scope — §1.)

---

## 4. New pieces to build

### 4.1 `VulkanDescriptorCache::UpdateSampledImage` (the one new low-level primitive)

Texture reload must keep the **same bindless index** so every material's
descriptor index stays valid and the swap is transparent to materials. Add:

```cpp
// Rewrites the descriptor at an existing bindless index to point at a new
// image/sampler, without allocating a new index. Used by texture hot reload
// so material descriptor indices stay valid across a swap.
void UpdateSampledImage(BindlessImageIndex index, ImageHandle image, VkSampler sampler);
```

Implement it by factoring the `VkWriteDescriptorSet` write that
`RegisterSampledImage` already performs, targeting `index.Value` instead of a
freshly allocated slot.

**VERIFY (risk — see §8):** the bindless set must be created with
`VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` (and the pool with
`VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT`) for it to be legal to
rewrite a descriptor that command buffers from the previous frame may have
referenced. Check `VulkanDescriptorCache`'s set/pool creation. If the flags
are absent, either add them (preferred — bindless arrays normally use them) or
gate the descriptor rewrite behind a `vkDeviceWaitIdle` on reload (acceptable
for a dev-only, low-frequency feature as a fallback).

### 4.2 Cache `ReloadInPlace` primitives (one per type)

Each cache gains a method that swaps the contents of an existing **resident**
entry, keeping slot/generation/refcount, and defers the old GPU resource.
Return `false` if the path has no live entry (the asset isn't resident — the
caller then just refreshes the registry hash; nothing to swap).

```cpp
// TextureCache
[[nodiscard]] bool ReloadInPlace(std::string_view path, const TextureData& image);
//  - find entry by path (FindRegisteredHandle); not live → false
//  - upload a new GPU image (same path as OnLoad/CreateFromTextureData)
//  - descriptors.UpdateSampledImage(entry.Bindless, newImage, sampler)  // SAME index
//  - images.Destroy(entry.GpuImage)   // old image → deletion queue (deferred)
//  - entry.GpuImage = newImage; entry.Extent = ...;  // keep Bindless/Gen/RefCount/PathKey
//  NOTE: TextureEntry does not currently store its SamplerDesc/VkSampler.
//        Store it on first load so reload can reuse it (small entry addition).

// StaticMeshCache
[[nodiscard]] bool ReloadInPlace(std::string_view path, const MeshGeometry& data);
//  - find entry; not live → false
//  - UploadMeshGeometryToGpu(*Buffers, data, newMesh, Log)
//  - DestroyGpuMesh(*Buffers, entry.Mesh)   // old buffers → deletion queue
//  - entry.Mesh = std::move(newMesh);        // keep Gen/RefCount/PathKey

// MaterialCache
[[nodiscard]] bool ReloadInPlace(std::string_view path, const Material& material,
                                 std::vector<TextureCacheHandle> ownedTextures);
//  - find entry; not live → false
//  - entry.Value = material;
//  - entry.OwnedTextures = std::move(ownedTextures);  // old vector destructs →
//                                                     // releases old texture refs
//    (no GPU resource of its own; CPU + owned refs only)
```

These are deliberately small and symmetric with the existing
`CreateFromData`/`Register` paths — they reuse the same upload helpers, just
target an existing slot.

### 4.3 Loader `CommitReload` (mirror of `CommitTyped`)

Keep "payload → cache" knowledge in the loaders. Add to each of
`TextureAssetLoader`, `StaticMeshAssetLoader`, `MaterialAssetLoader` a method
that mirrors `CommitTyped` but calls the cache's `ReloadInPlace` instead of
create/register:

```cpp
// Owner-thread. Swaps the staged payload into the existing resident entry.
// Returns false if the asset is not resident (nothing to swap).
bool CommitReload(AssetStaging&& staged);
```

For materials, factor the shared "MaterialDescription → (Material, owned
textures)" resolution out of `CommitTyped` so both create and reload use it.
Expose `CommitReload` through `AssetSystem::*LoaderRef()` (those accessors
already exist) for the reload driver.

### 4.4 Single-source re-import helper (cook)

Factor the per-source body of `ImportAssetsOnDemand` into a reusable function:

```cpp
// assets/cook/ImportOnDemand.h (SENCHA_ENABLE_COOK)
// Re-imports one changed source: runs its importer, writes artifacts under
// .cooked/, updates the cooked index on disk, and registers/refreshes the
// artifacts in the registry (RegisterOrVerify with the new ContentHash).
// Returns the set of cooked artifact virtual paths produced (so the caller
// can reload the resident ones). Owner-thread (touches registry + index).
[[nodiscard]] bool ReimportOneSource(std::string_view rootDirectory,
                                     std::string_view sourceRelPath,
                                     const AssetImporterRegistry& importers,
                                     AssetRegistry& registry,
                                     CookedCacheIndex& index,
                                     LoggingProvider& logging,
                                     std::vector<std::string>& outArtifactPaths);
```

`ImportAssetsOnDemand` should be refactored to call this in its loop (no
behavior change — keeps one code path, the existing tests still pin it).

### 4.5 `AssetHotReloadWatcher` (the new component, dev-only)

```cpp
// assets/hotreload/AssetHotReloadWatcher.h   (compiled only under SENCHA_ENABLE_COOK)
//
// Polls watched source files for content changes and drives in-place reloads.
// Owner-thread only (registry, caches, and the cooked index are owner-thread).
class AssetHotReloadWatcher
{
public:
    AssetHotReloadWatcher(LoggingProvider& logging,
                          AssetSystem& assets,
                          AssetRegistry& registry,
                          const AssetImporterRegistry& importers,
                          AsyncTaskQueue& tasks,
                          std::string assetsRoot);

    // Builds the watch set: every file under assetsRoot whose extension has an
    // importer, plus authored runtime assets that are loaded directly (.smat).
    // Records each file's last-write-time (and content hash on first sight).
    void Initialize(const CookedCacheIndex& index);

    // Call once per frame (or throttle internally to ~every 0.25–0.5s). Stats
    // the watch set; for each changed file, kicks off a reload (§5).
    void Poll();

private:
    // ... watch entries: { path, lastWriteTime, lastContentHash } ...
};
```

Polling strategy: `std::filesystem::last_write_time` over the watch set,
throttled (don't stat every frame). On an mtime change, confirm with a content
hash (`HashFileContents`) to avoid reacting to touch-only saves. Portable, no
OS-specific inotify/FSEvents/ReadDirectoryChangesW needed — a few hundred
stats every ~300ms is negligible and keeps the code one simple loop.

---

## 5. The reload flow (per changed source `S`)

All steps are owner-thread except the explicitly async decode.

1. **(cook sources only)** Re-import `S`: `ReimportOneSource(...)` → new
   cooked artifact bytes written to `.cooked/`, cooked index + registry
   `ContentHash` updated, `outArtifactPaths` = the virtual paths `S` produces.
   For an authored `.smat`, skip this step; the "artifact path" is `S`'s own
   virtual path, and only the registry `ContentHash` is refreshed.

2. **For each affected virtual path `P`** (the cooked artifacts, or the `.smat`
   path):
   - Look up `registry.FindByPath(P)` → `Type`.
   - Ask the matching cache whether `P` is **resident** (e.g.
     `assets.TryAcquireSkinnedMesh`-style `Find`, or a non-refcounting
     `IsResident(P)`; prefer a `Find` that does **not** bump refcount). If
     **not resident**, do nothing further — the refreshed registry hash means
     the next normal load picks up the new bytes. If **resident**, continue.
   - **Submit an async staged decode** mirroring the preloader:
     ```cpp
     IAssetLoader* loader = assets.LoaderFor(type);
     tasks.Submit<AssetStaging>(
         [loader, src = &assets.DefaultSource(), record]() {  // task thread
             return loader->LoadStaged(record, *src);          // pure decode
         },
         [this, type, P](AssetStaging staging) {                // owner thread, drain
             // CommitReload swaps in place; logs + skips on failure.
             CommitReloadFor(type, P, std::move(staging));
         });
     ```
   - `CommitReloadFor` dispatches to the right loader's `CommitReload` (§4.3),
     which calls the cache's `ReloadInPlace` (§4.2). The old GPU resource is
     enqueued on the deletion queue inside `ReloadInPlace`.

3. **Material cascade is automatic.** If `S` is a PNG, the texture entry's
   image is swapped *in the same bindless index* — every material pointing at
   that index renders new pixels next frame with **no material reload**. If
   `S` is a `.smat`, only that material reloads. The two never need to
   coordinate.

**Failure policy:** a failed re-cook or decode logs an error and leaves the
live asset exactly as it was (reload is best-effort; never tears down a good
resource on a bad edit). This matches the preload "advisory failure" stance.

---

## 6. Frame / threading model (why it's safe)

Per frame, in order:
1. `BeginFrame` → `VulkanDeletionQueueService::AdvanceFrame()` retires
   resources enqueued `framesInFlight + 1` frames ago (already happens).
2. Watcher `Poll()` (throttled): detect changes → re-cook (owner thread) →
   submit async decode tasks.
3. Drain point (owner thread, same place preloads/zone loads commit): completed
   decode tasks run `CommitReload` → build new GPU resource, rewrite the
   bindless descriptor / swap buffers, **enqueue the old resource on the
   deletion queue**, keep the slot/handle.
4. Render records command buffers against the now-updated slot.

Safety argument:
- **Handle stability:** the slot index + generation never change, so every
  outstanding handle resolves to the same slot with new contents → "zero
  handle invalidation."
- **No use-after-free:** the old image/buffer is only *enqueued* at commit;
  the deletion queue holds it `framesInFlight + 1` frames, by which point the
  fence wait in `BeginFrame` proves no in-flight command buffer references it.
- **Descriptor rewrite legality:** `UpdateSampledImage` runs at the commit
  point (between frames, before this frame's command recording). With
  `UPDATE_AFTER_BIND` on the bindless set this is legal even while older
  frames' command buffers reference the binding (they read the *old image*,
  which is still alive in the deletion queue). See §8 if the flag is missing.
- **Refcount untouched:** `ReloadInPlace` does not change `RefCount`; all
  component-held references survive.

The re-cook (importer) runs synchronously on the owner thread at poll time —
acceptable: a save is a rare, human-paced event, and the import writes a few
files. Only the *decode* (`LoadStaged`, e.g. BC7 of a texture, glTF parse) is
offloaded to the task lane, per Decision H. (A v0 may run the decode
synchronously too; see §9.)

---

## 7. Wiring

- New folder `engine/include/assets/hotreload/` + `engine/src/assets/hotreload/`,
  filtered out of the build when `SENCHA_ENABLE_COOK` is OFF (same
  `engine/CMakeLists.txt` mechanism that already filters `assets/cook/**`).
- `CubeDemoGame::OnStart` (under the existing `#ifdef SENCHA_ENABLE_COOK`
  block that builds the `AssetImporterRegistry`): construct the watcher,
  `Initialize(cookedIndex)`, store it on the game.
- Tick `watcher.Poll()` once per frame from the demo's update (or hook a
  per-frame callback in `RuntimeFrameLoop`/`Engine` if cleaner). Ensure it runs
  *after* `BeginFrame`'s deletion-queue advance and that reload commits land at
  the normal async drain point (don't invent a second drain).

---

## 8. Risks / things to verify before/while implementing

1. **Bindless `UPDATE_AFTER_BIND` flags** (§4.1) — the single most important
   verification. If the descriptor set/pool lack the update-after-bind flags,
   rewriting a live bindless slot is UB. Add the flags, or fall back to a
   `vkDeviceWaitIdle` around the descriptor rewrite for the dev-only path.
2. **TextureEntry sampler** — `TextureEntry` doesn't store its `VkSampler`/
   `SamplerDesc`; `UpdateSampledImage` needs it. Store it on first load.
3. **Residency check without refcount churn** — use a `Find`-style lookup that
   does not bump/drop refcount when deciding whether to reload. A stray
   acquire/release around a refcount-1 asset could free it mid-reload.
4. **Re-cook determinism** — `ReimportOneSource` must update the cooked index
   on disk and the registry `ContentHash` together, so a second edit re-cooks
   from the right baseline and the next cold start is consistent.
5. **Multi-artifact sources** — a multi-mesh `.glb` produces several artifacts;
   reload each resident one (the cooked index gives the set).
6. **Throttle the poll** — don't `stat` the whole tree every frame; ~300ms is
   plenty and avoids IO churn.

---

## 9. Suggested implementation order (sub-stages, each independently testable)

- **6a — Texture reload (the gate path). LANDED (2026-06-13).**
  `VulkanDescriptorCache::UpdateSampledImage` (in-place bindless rewrite;
  the set was already UPDATE_AFTER_BIND), `TextureCache::ReloadInPlace`
  (+ shared `UploadGpuImage` helpers, sampler stored on the entry),
  `TextureAssetLoader::CommitReload`, `ReimportOneSource`, the
  `AssetSourceWatcher` (detection) + `AssetHotReloader` (reaction) split, and
  the CubeDemo wiring (a throttled `HotReloadPollSystem`, ~300 ms). Headless
  tests in `test/core/HotReloadTests.cpp` pin watcher change-detection
  (content-confirmed, touch-only ignored) and `ReimportOneSource`. *Gate met:
  overwriting `assets/textures/dev/checker.png` while CubeDemo runs swaps the
  cube's texture in place — log shows `reloaded texture
  'asset://textures/dev/checker.png'`, the red material is untouched, and the
  Vulkan validation layer reports nothing (clean deletion queue, legal
  descriptor rewrite). 846 tests green.*
- **6b — Static mesh reload.** `StaticMeshCache::ReloadInPlace`,
  `StaticMeshAssetLoader::CommitReload`, watcher learns `.glb/.gltf/.blend`.
  *Gate: edit/re-export a mesh source → geometry swaps, same handle, old
  buffers deferred.*
- **6c — Material reload.** `MaterialCache::ReloadInPlace`, factor the
  description→Material resolution, `MaterialAssetLoader::CommitReload`, watcher
  learns `.smat`. *Gate: edit `red.smat`'s base-color factor → color updates
  live; a `.smat` that points at a different texture re-resolves and retains
  the new texture (old texture ref released).*

**Headless tests (zero-thread, no GPU where possible):**
- `ReimportOneSource` recooks a changed source and updates index + registry
  (extend `test/core/AssetImportTests.cpp`).
- `MaterialCache::ReloadInPlace` swaps value + owned-texture refs and releases
  the old refs (fully headless with the counting fake texture cache already
  used in `MaterialAssetTests.cpp`) — pins the refcount handoff.
- Watcher change-detection (mtime + content-hash) over a temp dir, with a fake
  reload sink, asserting it fires once per real content change and not on
  touch-only saves.
- The GPU swap halves (texture/mesh) follow the 4b/4c precedent: their commit
  needs a device, so they're covered by the live CubeDemo gate, not headless.

**Deferred (record, don't build):** skinned-mesh and audio-clip reload (same
machinery, add when needed), scene/`.json` reload, file create/delete
(topology) handling, OS-native file watching (the poll is sufficient).

---

## 10. Doc updates on completion

- Add a "Stage 6 status" section to `docs/assets/pipeline.md` mirroring the
  Stage 5 status format (what landed, the gate result, recorded deferrals).
- Note the `UPDATE_AFTER_BIND` decision and the poll-throttle interval as
  settled choices.
