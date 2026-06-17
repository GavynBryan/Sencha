# Asset Pipeline: Architecture Overview

This document explains what the asset pipeline classes are, why each exists,
and how they relate to each other. The planning rationale and design decisions
live in `pipeline.md`; this is the map you read when you sit down in front of
the code and need to know where things are and why.

---

## The problem

The engine has ~7 asset types (static meshes, skinned meshes, textures,
materials, skeletons, animation clips, audio clips). Every one of them needs
the same lifecycle:

1. Find the file (registry lookup).
2. Read bytes from disk (or a pack file, eventually).
3. Decode into CPU data (mesh geometry, image pixels, JSON parse, etc.).
4. Upload to GPU or register in a CPU-side cache.
5. Deduplicate by path — loading the same asset twice returns the same handle.
6. Reference-count so the asset stays alive while anything uses it and frees
   when nothing does.
7. Free resources (GPU teardown, slot recycling) when the refcount hits zero.

On top of that, the engine streams zones asynchronously: steps 2-3 must run on
worker threads without touching engine state, and step 4 must happen on the
owner thread at a metered drain point.

Some assets reference other assets: a material references textures, a skinned
mesh references a skeleton, an animation clip references a skeleton. Those
cross-references create ordering constraints (you must load textures before
committing materials) and ownership chains (when a material frees, its texture
refs must release too).

The class count comes from solving this problem honestly across 7 types. There
are 4 layers, and each layer fans out by type where the logic is genuinely
type-specific.

---

## Layer 1: Byte source

**Files:** `core/assets/AssetSource.h`

| Class | Role |
|-------|------|
| `IAssetSource` | Abstract interface: `ReadBytes(path) -> bytes`. |
| `FileAssetSource` | The only implementation today — opens a file and reads it. |
| `ReadAssetBytes()` | Helper that resolves a record's physical path before reading. |

**Why it exists:** Loaders receive bytes, not file paths. This seam is what a
future pack-file reader plugs into — swap the `IAssetSource` implementation and
no loader changes. The loaders don't know or care whether bytes came from loose
files, a pack file, or memory.

---

## Layer 2: The staged-load contract

**Files:** `core/assets/AssetLoader.h`, then one loader per type under
`assets/{type}/`

### The interface

| Class | Role |
|-------|------|
| `IAssetLoader` | Abstract base with two methods: `LoadStaged()` and `Commit()`. |
| `AssetStaging` | The payload that flows between the two halves: an `AssetRecord`, a type-erased `std::any Payload`, and an error string. |
| `AssetCommitResult` | Success/failure result from `Commit()`. |

### The two halves

Every asset load splits into two steps:

- **`LoadStaged(record, source) -> AssetStaging`** — Pure CPU work. Reads
  bytes, decodes them into a plain data struct (e.g. `MeshGeometry`, `Image`,
  `MaterialDescription`). Touches no engine state — no caches, no GPU, no
  services. Safe to run on any thread. Errors go into `AssetStaging::Error`
  rather than logging, because this half might not be on the owner thread.

- **`Commit(staging) -> AssetCommitResult`** — Owner-thread only. Takes the CPU
  payload from staging, uploads to GPU if needed, inserts into the cache,
  returns the handle. This is where engine state changes.

The synchronous path calls both back-to-back on the same thread. The async path
runs `LoadStaged` on a task thread and `Commit` at the drain point. Same code,
two schedulings.

### The concrete loaders

| Loader | Payload type | What Commit does |
|--------|-------------|------------------|
| `StaticMeshAssetLoader` | `MeshGeometry` | GPU buffer upload via `StaticMeshCache`. |
| `SkinnedMeshAssetLoader` | `SkinnedMeshData` | GPU upload + resolves skeleton ref, holds owned `SkeletonHandle`. |
| `TextureAssetLoader` | `Image` or `TextureData` | GPU image upload + bindless descriptor slot via `TextureCache`. |
| `MaterialAssetLoader` | `MaterialDescription` | Resolves texture slots through `AssetSystem` (loads or cache-hits), registers `Material` with owned `TextureHandle`s. |
| `SkeletonAssetLoader` | `SkeletonData` | CPU-only registration in `SkeletonCache`. |
| `AnimationClipAssetLoader` | `AnimationClipData` | Resolves skeleton ref, registers with owned `SkeletonHandle`. |
| `AudioClipAssetLoader` | `AudioClip` | CPU-only registration in `AudioClipCache`. |

**Why 7 separate classes instead of a generic one?** Because the decode logic
(mesh binary deserialization vs JSON parsing vs image decompression vs audio
decoding) and the commit logic (GPU buffer upload vs GPU image upload vs
CPU-only cache insert vs resolving cross-asset refs) are genuinely different per
type. A single generic loader would just push type-specific code into callbacks
or template parameters — same total complexity, more indirection.

**Material is the interesting one:** its `Commit` calls back into `AssetSystem`
to load/resolve textures. On the sync path this may decode textures inline; on
the async path the preloader stages textures first (wave 1) so material commits
hit warm caches (wave 2). This dependency drives the two-wave design in the
preloader.

Each loader also has a `CommitReload()` method for hot-reload (dev-only): it
swaps new data into an existing cache slot in place, so handles never change
and components see updated data through their existing references.

---

## Layer 3: Caches

**Files:** `core/assets/AssetCache.h` (CRTP base), then one cache per type
scattered by domain — render caches under `render/`, GPU caches under
`graphics/vulkan/`, animation caches under `anim/`, audio under `audio/`.

### The CRTP base

`AssetCache<TDerived, THandle, TEntry>` provides all the shared machinery:

- **Generational slot pool:** Entries live in a flat vector. Freed slots go to a
  free list and get reused with a bumped generation counter. A handle is an
  `(index, generation)` pair — stale handles that point at a recycled slot are
  rejected because the generation won't match.

- **Path-based deduplication:** A `PathLookup` map ensures that loading the same
  path twice returns the same handle (with the refcount incremented).

- **Reference counting:** `Acquire()` increments, `Release()` decrements. When
  the count hits zero, `OnFree()` is called and the slot returns to the pool.

- **RAII handle integration:** Implements `ILifetimeOwner` so `Owned<THandle>`
  wrappers can automatically call `Release()` on destruction.

The derived class provides three hooks via CRTP (no virtual dispatch):

- `OnLoad(path, entry)` — populate the entry from a path.
- `OnFree(entry)` — release resources (GPU teardown, etc.).
- `IsEntryLive(entry)` — is this slot occupied?

### The concrete caches

| Cache | Entry holds | GPU? | Owns refs to |
|-------|------------|------|-------------|
| `StaticMeshCache` | `MeshGeometry` + `GpuStaticMesh` | Yes (vertex/index buffers) | Nothing |
| `SkinnedMeshCache` | `MeshGeometry` + `MeshSkinning` + `GpuStaticMesh` | Yes | `SkeletonHandle` |
| `TextureCache` | `TextureData` + Vulkan image + bindless index | Yes (GPU image) | Nothing |
| `MaterialCache` | `Material` + `vector<TextureCacheHandle>` | No | `TextureHandle`s |
| `SkeletonCache` | `SkeletonData` (joint hierarchy, bind poses) | No | Nothing |
| `AnimationClipCache` | `AnimationClipData` | No | `SkeletonHandle` |
| `AudioClipCache` | `AudioClip` (decoded PCM) | No | Nothing |

### Refcount chains

When a cache entry owns handles into another cache, freeing the parent
automatically releases the child refs:

```
MaterialEntry frees  ->  releases its TextureHandles  ->  textures free if refcount hits 0
SkinnedMeshEntry frees  ->  releases its SkeletonHandle
AnimationClipEntry frees  ->  releases its SkeletonHandle
```

This is why `RuntimeAssets` declares caches in a specific order — C++
destructs members in reverse declaration order, so caches that hold refs into
other caches must be declared *after* the caches they reference (destroyed
first):

```cpp
// RuntimeAssets member order (destruction is bottom-to-top):
AssetRegistry Registry;
TextureCache Textures;         // destroyed last among caches (materials ref it)
MaterialCache Materials;       // destroyed before textures
SkeletonCache Skeletons;       // destroyed after meshes and clips
StaticMeshCache StaticMeshes;
SkinnedMeshCache SkinnedMeshes; // destroyed before skeletons
AnimationClipCache AnimationClips; // destroyed before skeletons
AudioClipCache AudioClips;
AssetSystem Assets;            // destroyed first (refs all caches, but owns nothing)
```

---

## Layer 4: Orchestration

### `AssetSystem` — the front door

**File:** `core/assets/AssetSystem.h`

The single entry point for all asset operations. It owns all 7 loaders, holds
(non-owning) pointers to all 7 caches, and provides three families of methods:

- **`Load*(path) -> Handle`** — The synchronous path. Resolves the path through
  the registry, checks the cache for a dedup hit, calls `LoadStaged` then
  `CommitTyped` back-to-back, returns a ref-counted handle.

- **`TryAcquire*(path) -> Handle`** — Cache-only lookup. Returns a handle if
  the asset is already resident, invalid handle otherwise. Never loads. The
  preloader uses this to dedup against what's already in cache before submitting
  async work.

- **`Release*(handle)`** — Forwards to the appropriate cache's `Release()`.

It also exposes `LoaderFor(type)` and `DefaultSource()` for the async path —
the preloader calls these to get the right loader and byte source for task-
thread work.

### `RuntimeAssets` — the owner

**File:** `core/assets/RuntimeAssets.h`

A plain struct that owns the registry, all 7 caches, and the `AssetSystem`. Its
only job is construction (wiring everything together with the right Vulkan
services) and destruction (in the right order). It's the "one bag of asset
state" that the engine holds.

### `AssetPreloader` — the async driver

**File:** `core/assets/AssetPreloader.h`

Drives manifest-sized batches of async loads. Given a list of paths (typically
from a zone's asset manifest):

1. For each path: check the cache (dedup hit?), check in-flight table
   (coalesce with an existing load?), or submit a new `LoadStaged` task.
2. **Wave 1:** leaf assets — textures, meshes, skeletons, audio. These have no
   cross-asset dependencies, so they can all load and commit independently.
3. **Wave 2:** materials. Their commits resolve texture refs through
   `AssetSystem`, which now hits warm caches from wave 1 instead of decoding
   inline.
4. When the last asset commits, fire the `OnComplete` callback. The zone loader
   uses this to run its finalize step (entity deserialization), where components
   take their own handle refs.
5. The preload releases its scaffold refs — it was just keeping assets alive
   during the gap between commit and entity ownership.

`AssetPreload` is the per-request tracker that holds the scaffold handles and
counts pending assets. `AssetInFlightTable` coalesces duplicate in-flight
requests so two zones that share an asset don't double-load it.

---

## Identity and resolution

**Files:** `core/assets/AssetRef.h`, `AssetId.h`, `AssetIdMap.h`,
`AssetRegistry.h`, `AssetPath.h`

| Class | Role |
|-------|------|
| `AssetRef` | What gets serialized in scenes: a `Type` enum + a virtual path (`asset://...`). |
| `AssetId` | 64-bit stable identity. Survives renames. Assigned by the cook step, persisted in `asset_ids.json`. |
| `AssetRecord` | Registry entry: type, path, file path, id, content hash, version. |
| `AssetRegistry` | Path-to-record and id-to-record maps. Populated by scanning the assets directory. |
| `AssetIdMap` | Persisted `path -> (id, content_hash)` map that the cook maintains and runtime reads. |

Resolution order (id-first with path fallback): if the ref has an id and the
registry knows it, use the registry record's current path (survives renames).
Otherwise fall back to the path in the ref.

---

## Dev-only systems

### Cook and import (`assets/cook/`)

| Class | Role |
|-------|------|
| `IAssetImporter` | Abstract: source bytes in, cooked artifacts out. Pure — no engine state. |
| `AssetImporterRegistry` | Maps file extensions to importers. |
| `CookedCacheIndex` | Tracks source-hash -> cooked artifacts at `.cooked/index.json` for incremental re-cook. |

Importers exist for PNG textures, glTF meshes, Blender files (headless export
to glTF), and audio (WAV/OGG). Gated behind `SENCHA_ENABLE_COOK`.

### Hot reload (`assets/hotreload/`)

| Class | Role |
|-------|------|
| `AssetSourceWatcher` | Polls source files by mtime, confirms with content hash (ignores touch-only saves). |
| `AssetHotReloader` | Given a changed file: re-cooks if needed, then calls `CommitReload()` on the loader to swap data in place. Handles never change — components see new data through existing refs. |

---

## How to read the code

**Starting from a load call:** `AssetSystem::Load*()` is always the entry
point. Follow it to the loader's `LoadStaged` (decode) then `CommitTyped`
(cache insert + GPU upload).

**Starting from async loading:** `AssetPreloader::Begin()` fans out to task
threads. Follow `OnAssetCommitted` for the drain-point commit path.

**Adding a new asset type:** implement `IAssetLoader` (staged + commit),
derive from `AssetCache` (three CRTP hooks), register both in `AssetSystem`
and `RuntimeAssets`. The pattern is identical across all 7 existing types.

**Understanding ownership:** look at the cache entry structs. If an entry holds
an `Owned<SomeHandle>` or a `vector<SomeHandle>`, that's a refcount chain —
freeing the entry releases those refs.

**Understanding destruction order:** look at `RuntimeAssets` member declaration
order and its comment. C++ destroys in reverse order; caches that own refs into
other caches are declared after them.
