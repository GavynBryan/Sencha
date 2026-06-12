# Sencha Asset Pipeline: Plan

Status: **working plan** (branch `asset-pipelines`, started 2026-06-11). This is
the document where the decisions deferred from `docs/ecs/parallelization.md`
land. Decisions below are tagged:

- **Settled** — inherited from landed work or already proven in the tree.
- **Proposed** — a concrete recommendation, written to be disagreed with.
- **Open** — genuinely undecided; needs a product call before any code.

The product frame, restated from the parallelization doc because it grades
everything here: Sencha targets Metroidvania/Zelda-likes, survival horror, and
tentatively open world. Room-sized zones, 2–4 live, streamed with constant
backtracking churn. The asset pipeline's flagship job is therefore **feeding
`AsyncZoneLoader` without hitching the frame** — not maximizing cold-load
throughput, not editor asset databases. Everything else is in service of that,
plus the standing engine values: legible to humans, extensible without
ceremony, and a developer experience that does not fight the game team.

---

## What exists today (inventory, 2026-06-11)

The asset layer is real but narrow. An honest map:

| Piece | State |
|-------|-------|
| `AssetRef` (`core/assets/AssetRef.h`) | Type tag + `asset://` path string. Path **is** the identity ("until AssetId is introduced", per its own comment). |
| `AssetRegistry` | Path → `AssetRecord` map. `ScanAssetsDirectory` walks a root by extension (`.smesh`, `.smat`, `.stex`, `.smap`). `ContentHash` and `Version` fields exist but nothing computes them. |
| `AssetCache<TDerived, THandle, TEntry>` | CRTP base: generational slots, path dedup, refcounts, `LifetimeHandle` RAII. **Single-threaded by contract** — no locks, owner-thread-only. |
| `AssetSystem` | The front door. Covers StaticMesh (File + Procedural) and Material (**Procedural only** — `.smat` file loading is an error stub, as are Generated and Embedded for both types). |
| `StaticMeshCache` | Decode and GPU upload are **coupled**: `CreateFromData` uploads via `VulkanBufferService` inline, main thread. |
| `TextureCache` (`graphics/vulkan`) | Exists, works, has `CreateFromImage` — but is **not wired** into `AssetSystem` or `RuntimeAssets`. `AssetType::Texture`/`.stex` are recognized by the scanner and then go nowhere. |
| `ImageLoader` | stb_image → RGBA8/RGBA8_SRGB CPU `Image`. Free function, no ambient state — already async-lane-safe. |
| `Material` | CPU descriptor; references its texture as a raw `BaseColorTextureIndex` (`UINT32_MAX` = none), not an `AssetRef` or `TextureHandle`. |
| `AudioClipCache` / `AudioClipLoader` | Parallel universe: own handle type, own loader, not registered with `AssetSystem`. `AssetType::Audio` exists in the enum only. |
| Shaders (`VulkanShaderCache`) | Own pipeline with dev-only hot reload (glslang behind `SENCHA_ENABLE_HOT_RELOAD`). Deliberately outside the asset registry. |
| Scenes | JSON via `SceneSerializer`; `SceneSerializationContext` resolves `AssetRef`s through `AssetSystem` — which **acquires from caches**, hence main-thread-only, hence the `finalize` callback on `AsyncZoneLoader::BeginLoad`. |
| Lifetime | Component traits retain/release through World resources (e.g. `StaticMeshComponentAssets`); refcount-zero frees immediately; runtime Vulkan destruction goes through the deletion queue. |
| First consumer | CubeDemo loads its zone async but **pre-registers procedural assets on the main thread before submitting** — the exact gap this plan closes. |

## The inherited ledger

What `docs/ecs/parallelization.md` decided *about* assets and deferred *to*
this plan. These are constraints, not options:

1. **Work/commit split.** Async work stages read + decode to CPU-side staging
   data and touch no ambient state; commits insert into caches and do GPU
   uploads on the main thread at the drain point. Caches and Vulkan services
   stay single-threaded in v1.
2. **Budget-metered commits.** The drain budget (`AsyncCommitBudgetMs`, 2 ms
   default) cannot split a commit. Large asset payloads must be shaped as
   **multiple chunked tasks** so the budget can meter between them.
3. **Main-thread upload is v1; a transfer queue is a profile-gated later.**
   Decode is the expensive part; upload is the cheap remainder.
4. **Handles resolve on the owner thread.** Build callbacks capture handles by
   value; cache acquisition lives in `finalize`/commit.
5. **Unload follows the deletion queue**, and the expected unload-side design
   is `DetachZone` + sole-ownership handoff to an async task (when a save
   system exists).
6. **No genre-profile blobs.** Genre enters as default values on shape-neutral
   mechanism, never as named strategies.

## Goals

- A zone load streams **all** of its assets — meshes, textures, materials —
  through the async lane with zero missed fixed ticks. This is the gate that
  Stage B of the jobs plan defined and could not yet test honestly.
- One coherent front door: every asset type a scene can reference resolves
  through the same identity, registry, and load path. No more parallel
  universes per type.
- Adding an asset type is a bounded, documented exercise (the `AssetCache`
  CRTP already proves the cache half; the load half needs the same treatment).
- Dev-loop UX: edit a source file, see it in the running game without a
  restart (dev builds), without hand-maintaining manifests.

## Non-goals

- **No editor asset database.** No import GUIs, no thumbnail caches, no
  dependency graphs beyond what scene → asset references require.
- **No streaming virtual texturing / LOD systems.** Room-scale art does not
  need them; nothing below forecloses them.
- **No general dependency-graph build system.** Cook steps are per-asset-type
  functions, not a DAG engine.
- **No thread-safe caches.** Publish-by-handoff is the engine's concurrency
  model; we keep it. A lock in `AssetCache` is a design smell here.

---

## Decisions

### A. Asset identity — paths now, content hash for integrity, IDs in Stage 4

**Proposed; revised after product input (2026-06-11).**

`AssetRef.Path` stays the primary identity. The repo already serializes paths
in scenes, dedups caches by path, and keys the registry by path; the cost of
GUIDs (an indirection table, an authoring story for generating and never
colliding them, migration of every scene file) buys rename-safety we do not
need until an editor exists that renames things.

What we take now, because it is cheap and the fields already exist:
`ScanAssetsDirectory` computes `ContentHash` (xxhash-style, over file bytes)
and fills `Version`. That gives cooked-data invalidation (Decision B) and
hot-reload change detection (Decision H) one shared mechanism, and it makes
`RegisterOrVerify` mean something.

**Revision (2026-06-11):** the revisit trigger is already live — editor
Phase 1 is in development on a separate branch, and an editor that renames or
moves assets is no longer hypothetical. The `AssetId` groundwork therefore
moves from "deferred" to **scheduled in Stage 4**, while the scene corpus is
still one demo file: the cook step assigns stable ids and stamps them into
cooked scenes and manifests; paths remain the human-facing alias and the
dev-build fallback resolver. The id scheme must be coordinated with the
editor branch before Stage 4 starts — two branches inventing identity
independently is the one way this gets expensive.

Coordination check (read `editor-phase-1` directly, 2026-06-11): the editor
has **no identity scheme of its own** — it reuses the engine's scene JSON
wholesale (`SaveSceneJson`/`LoadSceneJson` on its own document `Registry`),
adds one editor-only component serializer (`BrushComponent`, FourCC `BRSH`),
and lists asset browser / material assignment / prefabs as explicit Phase 1
non-goals. So `AssetId` design is currently unconstrained, and the contact
surface is exactly one thing: the **authored scene JSON format**, which the
editor writes and the cook step (Decision B) consumes. Whatever Stage 4 does
to scene refs must keep the editor's save → load round trip intact, including
editor-only component chunks it knows nothing about. Two forward notes: the
editor branch forked from the `ecs-overhaul` merge and predates the jobs
work, so it will rebase over this plan's changes, not vice versa; and brush
geometry compiled to meshes is the obvious eventual customer for
`AssetSourceKind::Generated`, which is currently an error stub.

### B. Import boundary — runtime formats in the repo, import-on-demand in dev

**Proposed**, with an **Open** sub-question.

Three candidate shapes:

1. **Offline importer tool** (gltf/png/wav → `.smesh`/`.stex`/… committed to
   the assets dir). Maximum shipping hygiene, worst dev loop — every art
   change is a tool invocation the developer must remember.
2. **Runtime import of source formats** (engine loads gltf directly, forever).
   Best first-contact UX, but it drags assimp-class dependencies into the
   runtime, makes load cost author-format-dependent, and violates the existing
   design note "load-time formats compile into runtime tables before hot-path
   use."
3. **Import-on-demand with a cooked cache**: dev builds resolve an asset by
   checking a `.cooked/` cache keyed by source `ContentHash`; on miss or stale,
   they run the per-type import function and write the cooked artifact.
   Shipping builds load only cooked artifacts. The importer is engine-side
   code but compiled into dev/tool targets only (the `SENCHA_ENABLE_HOT_RELOAD`
   precedent — glslang never ships).

Take 3. It is the one that satisfies "UX that doesn't fight the developers"
and "runtime loads runtime formats" simultaneously, and the per-type import
functions are exactly the code an offline cook tool would call anyway — so
option 1's batch tool falls out for free later (`sencha-cook <assets-root>`),
rather than being the thing the dev loop is built on.

Source formats (**Settled**, product input 2026-06-11): mesh sources are
**glTF 2.0 and `.blend`**; the runtime format remains our own `.smesh` (the
format, loader, and serializer already exist — import means glTF →
`StaticMeshData` → `StaticMeshSerializer::WriteToFile`). `.blend` is handled
by shelling out to headless Blender (`blender --background` + export script)
to produce glTF, so **everything funnels through one glTF import path**.
Blender is a dev-machine dependency of the cook step only — same never-ships
rule as glslang. Textures are PNG, audio WAV/OGG. The glTF parser choice is
an implementation detail, not a product call: cgltf (single-header, stb
precedent) is the proposed pick.

One consequence worth stating now: a single glTF/.blend source can yield
**multiple cooked artifacts** (several meshes; later skeletons and clips, see
Decision J). The cooked-cache keying is therefore source-hash → *set of
outputs*, not one-to-one, from day one.

### C. The staged-load contract — `IAssetLoader` work/commit split

**Proposed.** This is the centerpiece; everything else feeds it.

Each asset type provides a loader with two halves, mirroring the
`AsyncZoneLoader` build/finalize split exactly:

```cpp
// Sketch — names to taste. One per asset type.
class IAssetLoader
{
public:
    // Task thread. File IO + decode into plain CPU data. No caches, no
    // services, no Vulkan, no logging beyond the thread-safe provider.
    // Returns staging data by value (type-erased payload, same pattern as
    // AsyncTaskQueue::Submit<TPayload>).
    AssetStaging LoadStaged(const AssetRecord& record);

    // Owner thread, inside a drain commit. Insert into the cache, perform
    // the GPU upload, return the handle. Must respect chunking (below).
    AssetCommitResult Commit(AssetStaging&& staged);
};
```

Notes that are contractual, not stylistic:

- **`LoadStaged` is pure with respect to engine state.** `ImageLoader` and
  `StaticMeshLoader::LoadFromBytes` already meet this bar; the pattern is
  generalization, not invention.
- **The synchronous path is `LoadStaged` + `Commit` called back-to-back on the
  owner thread.** `AssetSystem::LoadStaticMesh` becomes exactly that
  composition. One code path, two schedulings — the same trick as
  `ThreadPoolJobSystem(0)` and `PumpWork`, and it makes every loader testable
  deterministically with zero threads.
- **Chunking is the loader's duty.** A texture atlas or fat mesh that would
  blow the 2 ms drain budget must be submitted as multiple tasks whose commits
  are individually small (e.g. staging buffer per N mip levels / per vertex
  range). v1 rule of thumb: one commit per asset, and we measure; the chunked
  shape only gets built when a profile shows a single commit exceeding the
  budget (same discipline as the Stage D gate).
- **Dedup happens before work is submitted.** The owner thread checks the
  cache (`Find`) and the in-flight table first; two zones requesting the same
  texture must coalesce on one load, with the second requester ref-counting
  the same pending entry. The in-flight table is owner-thread state — no
  locks, by the usual argument.

### D. Zone asset manifests — the scene's asset list as derived data

**Proposed.**

Today the only way to learn what assets a zone needs is to parse its scene
JSON — which happens on the task thread, while cache acquisition must happen
on the owner thread. CubeDemo squares this circle by pre-registering
everything by hand. The general fix:

- The cook step (Decision B) emits, next to each cooked scene, a **manifest**:
  the flat list of `AssetRef`s the scene references, derived by walking the
  serialized component fields (the `SceneFieldCodec` machinery already knows
  which fields are asset refs — that is how deserialization resolves them).
- `AsyncZoneLoader` gains an asset-aware load recipe: read the manifest on
  the owner thread (it is tiny), submit `LoadStaged` tasks for every asset not
  already live, submit the scene-build task, and let the existing drain
  ordering commit assets before the zone's finalize runs `LoadSceneJson` —
  which then finds every cache already populated.
- The manifest is **derived data, never authored**. Hand-maintained manifests
  rot; that is the "fights the developers" failure mode. In dev builds, a
  stale/missing manifest falls back to parse-then-load (slower, correct);
  shipping builds treat it as required.

This is also the preload primitive the genre wants: preloading the next room
is `BeginLoad` with a dormant attach, and the manifest is what makes the
asset half of that preload knowable without touching the scene file off-line.

### E. Texture and material pipeline — close the loop end to end

**Proposed**, mostly mechanical, and the right **Stage 1** because every piece
exists and merely is not connected:

- Wire `TextureCache` into `RuntimeAssets` and `AssetSystem`
  (`LoadTexture(path)`, plus the procedural register used by tests/demos).
- Implement `.smat` as a small JSON format (the enum says binary-shaped
  formats, but materials are ~5 fields of authored data; JSON matches scenes
  and keeps them hand-editable — `.smesh` stays binary because vertex data is
  bulk). An `.smat` references its texture by `AssetRef` path. The field set
  is the **Decision L PBR schema from the first commit** — Stage 1 authors
  only base color and leaves everything else defaulted, but there is never a
  pre-PBR `.smat` to migrate.
- `Material` keeps `BaseColorTextureIndex` as the **runtime** field (the
  shader-visible index), but material *load* resolves the texture ref →
  `TextureHandle` → descriptor index, and material *release* releases the
  texture. The cache-entry refcount machinery already supports this; it is
  the same retain/release pattern component traits use.
- CubeDemo's red/green/blue procedural materials become three `.smat` files
  and one PNG, exercising the whole chain.

On the `.stex` container (**Settled** direction, product input 2026-06-11:
plan for compression from day 1 without hardcoding or breaking anything):
the container is **format-tagged from its first version** — an explicit
pixel-format enum (RGBA8, RGBA8_SRGB, BC7, …) and a mip table with per-level
offsets and byte sizes. No consumer may assume bytes-per-pixel or an
uncompressed layout: the runtime upload path takes (format, extent, byte
span), and the `Image` → `TextureCache::CreateFromImage` seam widens to carry
the format tag rather than implying RGBA8. The cook step may *emit* RGBA8 +
mips first, but a BC-compressed fixture rides the test suite from the first
`.stex` commit so the compressed path can never rot into a de-facto
hardcoded assumption. The BC7 encoder (bc7enc-class, single-purpose) lands
with or shortly after the cook step.

### F. One front door — fold audio in, leave shaders out

**Proposed.**

- `AudioClipCache` migrates onto the `AssetCache` CRTP base and registers with
  `AssetSystem` (`LoadAudioClip(path)`); `AudioClipLoader` already has the
  pure-decode shape `LoadStaged` needs. The migration is the proof that
  Decision C's "adding a type is bounded" claim is true — if it is painful,
  the contract is wrong, and we find out on the cheapest possible type.
- One boundary to draw while migrating: this covers **clips** — fully
  resident, decode-once SFX. Streamed music is a different contract entirely
  (a long-lived decode source feeding a voice across frames, not a
  `LoadStaged`/`Commit` pair) and belongs to a future audio plan. The only
  act of discipline now is to not shape the type as if every audio asset is
  a resident clip — the cache is honestly named `AudioClipCache`; keep it
  that way, and let `AssetType` grow a distinct streaming-source tag when
  that plan exists rather than overloading `Audio`.
- **Shaders stay out.** `VulkanShaderCache` is keyed by pipeline state, has
  its own hot-reload path, and its consumers are render-internal. Forcing it
  through `AssetSystem` buys uniformity nobody asked for. Revisit only if
  gameplay-authored shaders (material graphs) ever become a product goal.
- `AssetType::Script` stays an enum value with no machinery until a scripting
  story exists at all.

### G. Lifetime under streaming churn — refcounts now, measure before caching

**Proposed.**

The shape of the worry: backtracking means room A unloads, room B loads, room
A reloads thirty seconds later — naive refcounting re-decodes and re-uploads
everything A and B share-nothing on every crossing.

The existing mechanism already blunts most of it: *shared* assets stay alive
because the neighboring preloaded zone holds refs. The churn cost is only the
assets unique to the evicted room. Room-scale uniqueness is small by
construction.

So: **keep refcount-zero-frees-immediately for now**, and make the unload path
correct (zone detach releases through component traits; GPU teardown rides the
deletion queue — both already the rule). Add an LRU "graveyard" (freed-but-
retained entries with a byte budget) **only when a profile of real
room-ping-pong shows re-decode cost that matters** — it is a contained,
per-cache feature, and building it before measuring would be the commit-count
mistake again. The doc records the design so it isn't re-derived: on
refcount-zero, move the entry to a budget-bounded tombstone list instead of
freeing; `Acquire` resurrects from it; eviction frees oldest-first.

### H. Hot reload — extend the shader pattern to content, dev-only

**Proposed**, staged late deliberately.

The mechanism is already proven twice over: a file watcher (dev-only, like
glslang) detects a source change, the import function re-cooks (Decision B
gives us this for free), an async task `LoadStaged`s the new bytes, and the
commit **swaps the cache entry in place** — same slot, same generation-checked
handle, new contents — so every live component handle picks up the new data
with zero invalidation protocol. GPU teardown of the replaced resource goes
through the deletion queue, which exists precisely for this.

The slot-swap trick is why hot reload is cheap here and agonizing elsewhere:
our handles are already indirection. What it cannot cover (archetype-changing
scene edits) is out of scope — that is editor territory.

### I. Packaging — design the seam now, build nothing

**Proposed-to-defer.**

`AssetRecord.FilePath` already separates virtual path from physical location,
which is the entire seam a pack-file reader needs. The one act of discipline
this plan imposes now: **loaders receive bytes, not paths** — `LoadStaged`
resolves `FilePath` through a trivial `IAssetSource` (v1: one implementation,
`open file, read bytes`). When shipping needs pack files, that is one new
`IAssetSource` and a cook output, with no loader changes. Nothing else —
no compression, no pack format — gets designed until a shipping target exists.

### J. Skeletal meshes and animation — asset-side in scope (added 2026-06-11)

Scope **Settled** (product call); shape **Proposed**.

Animated characters are not a later plan — the target genres live on them,
and the asset pipeline must carry them. The boundary that keeps this tractable:
**this plan owns formats, import, and caches; pose evaluation and skinning
runtime are a separate future plan.** Assets land first, and the format work
must not pre-decide runtime questions it cannot see yet.

Proposed shape:

- Three asset types, all imported from the same glTF source (skins +
  animations channels):
  - **Skeleton** (`.sskel`): joint hierarchy, joint names, bind and
    inverse-bind transforms. A shared, separately cached asset — many meshes
    and many clips reference one skeleton, by `AssetRef`, with the same
    refcount composition materials→textures use (Decision E).
  - **Skinned mesh**: extend `.smesh` rather than fork a sibling format — the
    header already carries `Flags` and reserved fields for exactly this; a
    skinned vertex stream (joint indices + weights) gates on a flag bit and a
    `Version` bump. The loader rejects skinned files it doesn't understand by
    version, which is the upgrade path the format reserved for itself.
  - **Animation clip** (`.sanim`): per-joint tracks referencing their
    skeleton by `AssetRef`.
- `AssetType` grows `Skeleton` and `AnimationClip` (skinned meshes stay
  `StaticMesh`-adjacent or get their own tag — decided when the component
  story exists); the scanner learns the extensions.
- Caches: skeleton and clip caches are CPU-side (`MaterialCache` pattern —
  the CRTP base does the work); the skinned mesh follows `StaticMeshCache`.
- Deliberately **not decided here**: clip storage (sampled vs. keyframed)
  and clip compression — those belong to the animation runtime plan;
  `.sanim`'s version field is the room they get to move in. Committing to
  track layouts before a single pose has been evaluated would be designing
  blind. How skinning reaches the GPU was originally deferred wholesale the
  same way; it is now **sketched but not chosen** in Decision N, because the
  asset formats need to know what either choice demands of them.

### L. Texture sets and the PBR material model (added 2026-06-11)

**Settled** scope (product call: plan the full PBR set now); shape
**Proposed**.

The current material is one base-color texture and the current shader is a
single hardcoded lambert light. The render feature ladder (shadows, punctual
lights, PBR shading) is a separate plan — but the *formats* are this plan's
problem, and authoring content twice is the failure mode to kill: assets
authored during Stage 1 must light up unchanged when the PBR shader lands.

**Texture usage classes.** `.stex` carries a **usage tag** alongside the
pixel-format tag from v1 (extending Decision E's format-tagged container).
Usage determines colorspace and the cook step's BC format choice; no
consumer ever guesses either:

| Usage | Colorspace | Cooked format (default) |
|-------|-----------|------------------------|
| BaseColor | sRGB | BC7_SRGB |
| Normal | linear | BC5 (two-channel, Z reconstructed in-shader) |
| ORM (occlusion-roughness-metallic) | linear | BC7 |
| Emissive | sRGB | BC7_SRGB |
| LinearData (masks, misc.) | linear | BC4/BC7 by channel count |

**Channel packing.** The cook step normalizes to **one packed ORM texture**
(R = occlusion, G = roughness, B = metallic — glTF's own layout). glTF
sources with a separate occlusion texture get merged at cook; the runtime
never sees unpacked variants.

**Mip policy.** The cook step generates full mip chains; the runtime never
generates mips. Downsampling is colorspace-correct (linearize → filter →
re-encode for sRGB usages) and normal maps are renormalized per level. The
mip table with per-level offsets is already in the `.stex` design (Decision
E); this decision just states who fills it and how.

**The `.smat` schema** is the glTF metallic-roughness model, our names:
`BaseColorFactor` + `BaseColorTexture`, `NormalTexture` + `NormalScale`,
`OrmTexture` + `RoughnessFactor` + `MetallicFactor`, `EmissiveFactor` +
`EmissiveTexture`, and `AlphaMode` (`Opaque` | `Mask` + cutoff | `Blend`).
Every texture slot is optional with a defined **neutral default** (white for
base color, flat +Z for normal, occlusion 1 / roughness 1 / metallic 0 for
ORM, black for emissive) — a material with no textures is still a complete
PBR material. `AlphaMode::Blend` maps to the reserved Transparent phase,
which has no pipeline yet: the cook accepts it, and the runtime **warns and
renders opaque** until that phase exists — content keeps authoring,
nothing crashes, the warning is the to-do list.

**Runtime `Material`** grows one descriptor index per slot (same
`UINT32_MAX` = neutral convention as today); the bindless array already has
the headroom. The current lambert shader consumes the base-color factor and
texture only; normal/ORM/emissive slots ride the asset and are ignored by
the shader, which is precisely the point: the data outlives the toy shading.

**Not decided here:** the PBR shading implementation, light culling, and
everything else on the render ladder — only what the bytes look like.

### M. Vertex format expansion — decided now (added 2026-06-11)

**Settled** (product call to decide now rather than reserve).

The current `StaticMeshVertex` is Position/Normal/UV0 (32 bytes, float).
Two expansions, decided here because the glTF importer (Stage 4/5) must
know what to emit:

- **Tangents join the base vertex**: `Vec4` — xyz tangent, w handedness
  sign, the glTF convention — bringing the base vertex to 48 bytes. Normal
  mapping (Decision L) is dead on arrival without them. Sources without
  tangents get them generated at cook (MikkTSpace — the de-facto standard,
  and a dev-only cook dependency under the same never-ships rule as glslang
  and Blender). This is a `.smesh` `Version` bump, taken once, in Stage 4,
  alongside the importer that needs it.
- **Skinning attributes are a separate stream, not interleaved**: joint
  indices (`u16 × 4`) + weights (`unorm8 × 4`, normalized at cook) — 12
  bytes per vertex, present only when the header's skinned flag is set (the
  flag-bit upgrade path Decision J reserved). A separate stream keeps the
  static path byte-identical and — decisively — serves **both** of Decision
  N's skinning options without re-cooking: vertex-shader skinning binds it
  as input attributes; compute pre-skin reads it as a storage buffer.
- **Deliberately not added**: vertex color and UV1. No consumer exists or
  is planned (no vertex-paint workflow, no lightmaps); each remains a
  reserved flag bit, addable later exactly the way skinning is being added
  now. Reserving differs from deciding: tangents have a customer in this
  plan, color/UV1 do not.

### N. How skinning reaches the GPU — the seam, sketched, not chosen (added 2026-06-11)

**Open by choice** (product call: record the options honestly, decide on the
animation runtime plan's schedule). This decision exists because "defer it
entirely" was quietly forcing format decisions anyway — the formats need to
know what either path demands.

The two real candidates:

1. **Vertex-shader skinning.** A skinned variant of each geometry pass
   reads the skinning stream as vertex attributes and a per-draw bone
   palette, poses in the vertex stage. One pass, no extra VRAM, the
   classical shape. Cost: every *future* geometry pass (shadow/depth,
   when the render ladder adds them) needs the skinned variant too, and
   pose math re-runs per pass.
2. **Compute pre-skin.** A compute pass poses each skinned instance into a
   per-instance posed-vertex buffer once per frame; everything downstream —
   extraction, sorting, the existing static-mesh draw path, every future
   pass — consumes it as if it were static geometry. Cost: VRAM per live
   skinned instance, one more dispatch phase, and a sync point.

The deciding inputs, named so the future decision is mechanical: **how many
geometry passes exist when the animation runtime lands** (each pass after
the first strengthens pre-skin), and **live skinned-instance counts at room
scale** (low by genre construction, so the VRAM cost is probably noise).
If shadows land before skinning does, pre-skin is likely winning — but that
is a forecast, not a decision.

What the **asset side guarantees regardless** — this is why Stage 5 can
ship formats before the choice is made:

- `.sskel` stores inverse-bind matrices; joint indices in the skinned
  stream are skeleton-local, resolved at cook.
- Weights are normalized at cook; the runtime never fixes data.
- Per-skin joint count is capped and recorded in the `.smesh` header, so
  either runtime can size palettes/buffers from the header alone.

What the **render side reserves regardless**, cheap today: the extraction
architecture already copies per-item data into `RenderQueueItem` — a bone
palette reference joins it as one more field; and the pipeline cache key
grows a vertex-input-variant dimension (needed for *any* second vertex
layout, skinned or not).

### O. Forecast asset types — name them now, build nothing (added 2026-06-11)

**Proposed-to-defer.** A genre gap analysis (action-adventure: animation
runtime, physics, navigation, save games, game UI) produced a list of asset
types this pipeline will eventually carry. None get machinery in this plan;
they are recorded so Stages 1–4 don't accidentally foreclose them, and
because each is a future test of the Decision C/F claim that adding a type
is bounded:

- **Entity templates (prefabs).** Component lists + values through the
  existing `TypeSchema` machinery, instantiated with per-instance overrides.
  The first asset type whose payload contains *both* asset refs and entity
  refs — manifest derivation (Decision D) must walk it like a scene
  fragment, not treat it as leaf data.
- **Data tables.** Rows of a `TypeSchema`-described struct keyed by stable
  id (damage tables, item definitions, tuning params). Trivial cache;
  hot reload (Decision H) is most of the point.
- **Collision shapes.** Cooked from the same glTF/`.blend` sources as render
  meshes — a second output of one source, which is exactly why Decision B
  keys the cooked cache source-hash → *set of outputs* from day one.
- **Navmesh.** Baked per zone by the cook step, listed in the zone's
  manifest (Decision D), streamed with the zone like everything else.
- **String tables (localization).** Wanted before any game UI ships,
  because retrofitting localization is the canonical expensive mistake.
- **VFX emitter descriptors.** Data-authored particle definitions.

The discipline these buy today is nearly free: keep `AssetType` cheap to
extend, keep manifest derivation schema-driven rather than hardcoded to the
component types we happen to have, and never let the loader contract assume
a payload is leaf data with no refs of its own — materials → textures
already broke that assumption; templates and tables break it harder.

---

## Rollout

Ordered, like the jobs plan, by return-on-complexity, each stage gated:

- **Stage 1 — close the texture/material loop (Decision E).**
  `TextureCache` wired, `.smat` JSON loading (the Decision L schema, base
  color authored, the rest defaulted), material→texture refcounts,
  CubeDemo on file-based assets end to end (synchronous path is fine here).
  *Gate: CubeDemo renders from `.smesh` + `.smat` + PNG with zero procedural
  registration; release of a zone releases the whole chain (asserted in a
  cache-state test).*
- **Stage 2 — the staged-load contract (Decisions C, I).**
  `IAssetLoader` split for mesh/texture/material, `AssetSystem` sync path
  recomposed on top of it, in-flight dedup table, byte-source seam.
  *Gate: every loader passes the zero-thread determinism suite; sync behavior
  bit-identical to Stage 1.*
- **Stage 3 — async zone loads carry their assets (Decision D).**
  Manifest emission, manifest-driven preload in `AsyncZoneLoader`, drain
  ordering verified. This closes the headline deferral from the jobs plan.
  *Gate: loading a textured, multi-mesh room over a running game produces
  zero missed fixed ticks and no drain-budget overruns; dormant preload of a
  neighbor room is invisible to frame spans.*
- **Stage 4 — import-on-demand, AssetId, audio unification (Decisions A, B,
  F, L, M).** Cooked cache keyed by content hash (source → set of outputs),
  glTF importer with the headless-Blender front end for `.blend` — emitting
  the Decision M base vertex (tangents generated via MikkTSpace where the
  source lacks them, `.smesh` version bump), PNG → `.stex` with usage tags,
  cook-side mip generation, and ORM channel packing per Decision L, WAV/OGG
  importers, `AudioClipCache` migration as the extensibility proof, and the
  `AssetId` groundwork — cook assigns stable ids into cooked scenes and
  manifests, scheme coordinated with the editor branch.
  *Gate: clean checkout + dev build + run = correct render with no manual
  cook step; second run hits the cooked cache (measured); a cooked scene
  resolves every ref by id with path as fallback; normal-map (BC5) and
  packed-ORM fixtures cook to their tagged formats and round-trip the
  texture path.*
- **Stage 5 — skeletal assets (Decisions J, M, N).** glTF skin/animation
  import → `.sskel` + skinned `.smesh` (the Decision M skinning stream) +
  `.sanim`, skeleton and clip caches, refcount chain (mesh→skeleton,
  clip→skeleton), and the Decision N asset-side guarantees (cook-resolved
  joint indices, normalized weights, joint count in the header).
  *Gate: an animated character source imports clean to all three artifact
  kinds; caches round-trip them; releasing the last reference frees the
  whole chain. Runtime playback is explicitly not gated here — that is the
  animation runtime plan's first gate.*
- **Stage 6 — hot reload (Decision H).** Watcher + recook + slot-swap for
  textures, meshes, materials. *Gate: edit a PNG while CubeDemo runs, see it
  next frame-ish, zero handle invalidation, deletion queue clean.*
- **Deferred, with their triggers:** LRU graveyard (room-ping-pong profile,
  Decision G), pack files (shipping target, Decision I), transfer-queue
  uploads (drain-budget profile, inherited ledger #3), chunked commits
  (single commit > budget, Decision C), clip storage/compression (animation
  runtime plan, Decision J), the skinning GPU path choice (animation
  runtime plan, criteria named in Decision N), PBR shading itself (render
  ladder plan — Decision L ships the data, not the lighting), streamed
  audio sources (audio plan, Decision F), and the forecast asset types
  (Decision O — each triggered by the runtime plan that owns it:
  templates/data tables by the gameplay-data plan, collision shapes by the
  physics plan, navmesh by the navigation plan, string tables by the UI
  plan).

## Stage 1 status (2026-06-11)

Landed, test-verified (707 tests green; 13 new in
`test/core/MaterialAssetTests.cpp`):

- `render/Material.h` — the runtime material is the full Decision L PBR
  model: four descriptor-index slots (`UINT32_MAX` = neutral), factor
  fields, `MaterialAlphaMode` + cutoff. The forward shader consumes the
  base-color factor and texture only, as Decision L specifies.
- `assets/material/MaterialFormat.h` + `MaterialLoader.{h,cpp}` — the
  `.smat` JSON parser: pure functions (no logging, no engine state),
  full Decision L schema, **unknown keys are errors** (a typo that silently
  falls back to a neutral default is the failure mode that fights the
  developer).
- Material→texture refcounts ride `LifetimeHandle`'s type-erased
  `ILifetimeOwner`: `MaterialEntry` owns `TextureCacheHandle`s (the alias
  moved to `render/TextureHandle.h` so it is backend-free), `OnFree`
  releases them, and the release chain is tested headlessly with a counting
  fake — no GPU in the test.
- `AssetSystem` gained `LoadTexture` (with an `srgb` parameter that exists
  only until usage-tagged `.stex` replaces it) and the File branch of
  `LoadMaterial`: parse, resolve slots to bindless indices, register with
  owned references. `alpha_mode: blend` warns and renders opaque, per
  Decision L. A missing `TextureCache` (headless tests) degrades to neutral
  slots instead of failing.
- `RuntimeAssets` owns a `TextureCache`, declared before `Materials`
  because destruction order is the release chain's correctness condition.
- Scanner maps `.png` → Texture as an explicitly-commented Stage 1 dev
  convenience (replaced by cooked `.stex` in Stage 4).
- CubeDemo: zero procedural registration. `ScanAssetsDirectory` at startup;
  three committed `.smat` files (red references a checker PNG, exercising
  the texture chain); `cube.smesh` is generated at build time by a
  `GenerateCubeDemoAssets` tool target — the seed of the Stage 4 cook step,
  and regeneration keeps the bytes in sync when the vertex format moves
  (Decision M). Verified end to end: scan registers 5 records, the scene
  loads through `.smesh` + `.smat` + PNG with zero errors under llvmpipe.

Deliberately not done, with reasons: no `.stex` yet (Stage 4); no
normal/ORM/emissive shader use or PBR lighting (the render ladder owns
those). The zone-level release-chain assertion is covered at cache level
(component-trait release was already tested; the new link — material frees
texture — is what the new tests pin).

## Stage 2 status (2026-06-11)

Landed, test-verified (719 tests green; 12 new in
`test/core/AssetLoaderTests.cpp`; CubeDemo verified unchanged on the
recomposed path):

- `core/assets/AssetSource.h` — the Decision I byte seam: `IAssetSource`,
  `FileAssetSource` (the v1 open-file implementation), and
  `ReadAssetBytes(record)` resolving `FilePath` with virtual-path fallback.
  Tests drive loaders through a `MemoryAssetSource` — no filesystem, no
  threads.
- `core/assets/AssetLoader.h` — the Decision C contract: `AssetStaging`
  (record + type-erased `std::any` payload + error string), `IAssetLoader`
  with the `LoadStaged` (task-thread, pure, errors-not-logs) / `Commit`
  (owner-thread, logs) split. Each loader also exposes a typed
  `CommitTyped` returning its handle; the virtual `Commit` wraps it for
  the heterogeneous driver Stage 3 needs.
- Three implementations: `StaticMeshAssetLoader` (payload `StaticMeshData`),
  `TextureAssetLoader` (payload `Image`; the srgb parameter lives on a typed
  overload, and the generic driver-facing overload assumes sRGB until
  `.stex` usage tags), and `MaterialAssetLoader` (payload
  `MaterialDescription`; slot resolution and the Blend warning moved here
  from `AssetSystem`). Materials are the first payload that references other
  assets — its commit loads textures through the front door, which the
  manifest path will have pre-staged (Decision O said this shape recurs).
- `AssetSystem` File branches are now literally dedup-check + `LoadStaged` +
  `CommitTyped` back-to-back — the sync path *is* the async path's two
  halves on one thread. `StaticMeshFileLoader` left `AssetSystem`; it lives
  inside the mesh loader now.
- `core/assets/AssetInFlightTable.h` — owner-thread coalescing bookkeeping
  (`Begin` → Started/Joined, `Finish` → requester count for retains).
  Built and tested standalone; deliberately not wired into `AssetSystem`,
  because the synchronous path has no in-flight window — Stage 3's
  manifest-driven loads are its first consumer.
- Gate honesty: mesh and texture *commit* halves need a GPU, so the
  zero-thread suite covers their stage halves (including malformed-bytes
  failures and a payload-type-mismatch contract test) plus clean null-cache
  commit failure; the material loader round-trips fully headless. Sync
  behavior is pinned by the Stage 1 tests passing unchanged.

## Stage 3 status (2026-06-11)

Landed, test-verified (731 tests green; 12 new across
`test/runtime/AssetPreloadTests.cpp` and the reshaped in-flight-table tests;
new tests TSan-clean including the threaded smoke). This closes the headline
deferral from the jobs plan: a zone's assets stream through the async lane,
and the zone attaches only when they are resident.

- `core/assets/AssetManifest.{h,cpp}` — `CollectAssetPaths` walks any JSON
  document for `asset://` strings, deliberately schema-agnostic: a future
  component that serializes an asset ref as a path string is covered
  without this code knowing it exists (the Decision O discipline). Manifest
  entries are paths only; the type always comes from the registry record,
  so the manifest can never contradict the registry.
- `core/assets/AssetPreloader.{h,cpp}` — the manifest driver: dedup against
  caches (`TryAcquire*`), coalescing against loads already in flight (the
  Stage 2 table, reshaped to carry waiters — its first consumer revealed
  that counting alone was the wrong API), `LoadStaged` on task threads,
  `CommitTyped` at the drain point, where every commit is individually
  metered by the existing `AsyncCommitBudgetMs`. **Two waves**: leaf assets
  (textures, meshes) first, materials submitted by the last wave-1 commit —
  so material commits always resolve their texture refs against warm caches
  instead of decoding inline at the drain.
- `AssetPreload` — the per-request tracker. Its handles are scaffolding:
  they keep assets alive (and deduplicated) between commit and the moment
  finalize's entities take their own references through component traits,
  then `ReleaseAll()` lets go. Failures are advisory and count toward
  completion — preload is an optimization; correctness always rests on the
  synchronous fallback.
- `AsyncZoneLoader::BeginLoad` gained the preload-gated overload: if the
  build commits before the assets land, the attach defers — still at the
  drain point, still owner-thread, fired by the preload's last asset commit.
  A cancelled preload counts as complete (attach proceeds, sync fallback
  covers gaps), so a preload can never wedge a zone. A zone whose build has
  committed but whose attach is deferred can no longer be cancelled.
- Manifest emission lives in `GenerateCubeDemoAssets` (the proto-cook):
  scene refs plus one level of `.smat` indirection, the same
  `CollectAssetPaths` walk for both. CubeDemo loads the manifest at startup,
  preloads, and passes the preload to `BeginLoad` — verified end to end with
  zero errors and zero fallback warnings; all five assets (mesh, three
  materials, the texture pulled in transitively through `red.smat`) stream
  through the async lane before the zone attaches.
- Gate honesty: the ordering, coalescing, refcount-exactness (entries free
  when the last holder releases), deferral, and cancellation properties are
  pinned by zero-thread deterministic tests. The wall-clock half of the gate
  (zero missed fixed ticks while streaming a textured multi-mesh room over a
  *running* game, dormant preload invisible to frame spans) needs a second
  room and frame instrumentation — CubeDemo has one zone loaded at startup.
  The mechanism for that measurement exists (FrameTrace, the Stage B missed-
  tick criterion); the measurement itself is owed when multi-room content
  exists, and the doc will record it then.

## Stage 4 status (2026-06-11, in progress)

Stage 4 is the widest stage and lands as five gated sub-stages:
**4a** foundations (content hashing, cooked cache, import-on-demand,
`SENCHA_ENABLE_COOK`), **4b** `.stex` + texture cook (Decisions E, L),
**4c** mesh (tangents, glTF/cgltf, MikkTSpace, headless Blender —
Decisions B, M), **4d** audio (WAV/OGG importers, `AssetSystem`
registration — Decision F), **4e** AssetId groundwork (Decision A; scheme:
a persisted id map maintained by the cook — new assets get an id at first
sight, renames keep theirs via the map).

One inventory correction discovered entering the stage: `AudioClipCache`
already sits on the `AssetCache` CRTP base, so 4d is registration plus an
`IAssetLoader` and OGG support — the "migration" half of the Decision F
proof is already done.

### Stage 4a — foundations (landed)

Test-verified (753 tests green; 22 new across
`test/core/ContentHashTests.cpp` and `test/core/AssetImportTests.cpp`):

- `core/hash/ContentHash.h` — `HashBytes64` (XXH64, implemented in-tree,
  ~130 lines; pinned against published reference vectors) and
  `HashFileContents`. Explicitly non-cryptographic; little-endian assumed
  (the cooked cache is per-checkout, not distributed).
- `ScanAssetsDirectory` now fills `AssetRecord::ContentHash` — the one
  shared mechanism Decisions B and H both key off — and skips the
  `.cooked/` directory (`kCookedCacheDirName`): cooked artifacts register
  under cook-assigned virtual paths, never by extension discovery.
- `assets/cook/CookedCache.{h,cpp}` — the cooked-cache index at
  `<assets-root>/.cooked/index.json`, keyed source-relative-path →
  (source hash, **set of artifacts**) per Decision B. Hashes serialize as
  hex strings (JSON numbers are doubles); serialization is sorted for
  deterministic diffs; a corrupt index is a cold cache, never an error.
- `assets/cook/AssetImporter.h` — the importer contract, mirroring
  Decision C's stage half: pure (bytes in via `ImportInput`, artifacts out
  via the `ICookOutputWriter` seam — tests run importers against memory),
  errors-not-logs, one source → many artifacts. Plus
  `AssetImporterRegistry` (extension → importer, owner-thread, duplicate
  claims rejected).
- `assets/cook/ImportOnDemand.{h,cpp}` — the Decision B dev-build driver:
  walk sources by importer extension, hash, serve fresh entries from the
  cooked cache (artifact files verified on disk), re-import on miss/stale,
  register every artifact with the registry. Artifacts must land under
  `.cooked/` — the driver rejects escapes. Per-source failures are logged
  and isolated; the return value stays strict for tools and tests.
- `SENCHA_ENABLE_COOK` (default ON, the dev posture): `assets/cook/**` is
  filtered from the build when OFF — same never-ships pattern as glslang.
  Future cook-only deps (cgltf, MikkTSpace, bc7enc, the Blender shell-out)
  gate on it.
- Gate: cold cache imports and registers; warm cache provably skips the
  importer (invocation-counted); changed source bytes and deleted artifacts
  both recook; corrupt index recooks everything; escape and failure paths
  pinned. No consumer is wired yet — CubeDemo still scans runtime formats
  directly; the first real importer (4b) flips it.

### Stage 4b — `.stex` + texture cook (landed)

Test-verified (764 tests green; 11 new in `test/core/TextureAssetTests.cpp`,
TSan-clean), Decisions E and L made real:

- `render/TextureData.{h,cpp}` — the widened upload seam: format-tagged,
  usage-tagged, mip-tabled CPU texture, backend-free. BC4/BC5/BC7(/sRGB)
  are first-class alongside RGBA8 from v1, and `ValidateTextureData` pins
  the structural invariants (contiguous mips, halving extents, exact blob
  coverage) so a malformed container is rejected, never patched.
- `assets/texture/TextureFormat.h` + `TextureLoader` + `TextureSerializer`
  — the `.stex` container and its pure read/write halves. The BC fixture
  rides the suite from this first commit: hand-built BC7_SRGB and BC5
  chains round-trip the serializer, so the compressed path can never rot
  into a de-facto RGBA8 assumption.
- `assets/cook/TextureCook.{h,cpp}` (SENCHA_ENABLE_COOK only) — the cook
  owns mip generation and BC compression; the runtime never generates mips
  for cooked content. Downsampling is colorspace-correct (the test pins
  linear-space averaging: a black/white checker mips to ~188, not the naive
  128) and normal maps renormalize per level (pinned: averaged tilted
  normals snap back to unit length). Usage comes from the
  authoring-convention stem suffix (`_n`, `_orm`, `_emissive`, `_mask`), and
  maps to the Decision L format table: BaseColor/Emissive -> BC7_SRGB,
  Normal -> BC5, ORM/LinearData -> BC7. `PngTextureImporter` emits the
  artifact under the **source's virtual path** — authored references never
  churn when the cooked format evolves.
- Runtime consumption: `VulkanImageService::UploadMips` (one staging pass,
  per-level copy regions, explicit chains only — it rejects GenerateMips
  images), `TextureCache::CreateFromTextureData` with the BC VkFormat
  mapping, and `TextureAssetLoader` now **sniffs container magic instead of
  trusting extensions** — a cooked artifact keeps its `.png` path while
  serving `.stex` bytes. The `srgb` parameter now applies only to loose
  image bytes; cooked textures carry their own tags.
- The scanner's Stage 1 `.png → Texture` mapping retired on schedule:
  source formats reach the registry only through import-on-demand. CubeDemo
  runs `ImportAssetsOnDemand` (PNG importer registered) before the scan,
  under `SENCHA_ENABLE_COOK`.
- Verified end to end under llvmpipe: cold run cooks the checker PNG into
  `.cooked/textures/dev/checker.png.stex` and streams the 5-mip chain
  through the async lane and `UploadMips` with zero errors; warm run serves
  from the cooked cache without invoking the importer.
- Known limits, recorded: LinearData currently cooks to BC7 because PNG
  decode always yields RGBA; the "BC4 by channel count" branch waits for a
  source format or metadata that preserves channel count. The renderer's
  4b validation slice samples base color only; normal/ORM/emissive sampling
  and PBR lighting remain render-ladder work.

### Stage 4c — mesh cook (landed)

Test-verified (782 tests green; 14 new in `test/core/MeshCookTests.cpp`, all
zero-thread except the one that deliberately shells out to real Blender and
skips where it isn't installed). Decisions B and M made real:

- **The Decision M vertex bump, taken once:** `StaticMeshVertex` grew
  `Vec4 Tangent` (xyz + handedness sign, the glTF convention; 32 → 48
  bytes), `.smesh` moved to version 2 (`kSmeshFormatVersion`), and the
  loader rejects any other version — there was no v1 content to migrate:
  the dev cube regenerates at build time, cooked meshes recook. Validation
  now pins the invariant every producer must meet: finite tangent, w
  exactly ±1 (`MeshCook.LoaderRejectsVersionOneSmesh` pins the version
  gate). The forward pipeline carries tangents via the binding stride but
  declares no attribute until a shader consumes them — the validation
  layer warns on attributes shaders ignore, and normal mapping is
  render-ladder work.
- `assets/cook/MeshCook.{h,cpp}` (SENCHA_ENABLE_COOK only) — the one glTF
  import path: cgltf (v1.15, single-header, the stb precedent) parses
  `.glb`/`.gltf`; each glTF mesh becomes a `StaticMeshData` with
  primitives as sections (`MaterialSlot` = primitive ordinal), geometry in
  mesh-local space. **Sources must be self-contained** (.glb or data: URI
  buffers); external `.bin` files are rejected with a pointed error,
  because the cooked cache keys staleness on one source file's hash and a
  sibling that changes without the source changing would rot it.
- **Tangents, three ways, every cooked vertex covered:** authored TANGENT
  streams pass through (w snapped to ±1 — normalization is the cook's
  job), sources with UVs get MikkTSpace (pinned commit, cook-only dep;
  de-index → generate → re-weld exact duplicates so flat geometry doesn't
  triple its vertex count), and UV-less sources get a deterministic
  normal-derived basis. The MikkTSpace handedness is pinned by test: a +Z
  quad with U along +X must yield T = +X, w = +1.
- **Artifact naming:** a single-mesh source keeps the source's virtual
  path (`asset://meshes/torus.glb` serves `.smesh` bytes — the texture-
  cook precedent; the loader already sniffs bytes, not extensions). A
  multi-mesh source emits `asset://<source>#<mesh-name>` per mesh — `#`
  cannot appear in scanned file paths, so cooked names never collide with
  real files. Caveat recorded: splitting a one-mesh file into several
  renames its artifact from `<source>` to `<source>#<name>`.
- `assets/cook/BlendCook.{h,cpp}` — the `.blend` front end: headless
  Blender (`--background --factory-startup --python-expr`, executable
  overridable via `SENCHA_BLENDER`) exports a temp `.glb`, which funnels
  through the glTF path under the `.blend` source's own virtual path.
  Blender is a dev-machine dependency of this importer only (the
  glslang/MikkTSpace never-ships rule); a machine without it fails just
  the `.blend` sources, isolated per source by the driver. This is the
  one importer that cannot be pure (temp file + subprocess by nature);
  everything after the export is the shared pure path. Verified against
  real Blender 5.1: factory default scene → one welded cube artifact.
- CubeDemo exercises the chain end to end: a committed `torus.glb`
  (UV'd, no authored tangents — the MikkTSpace path) joined the scene
  with the red material. Verified under llvmpipe: cold run cooks PNG +
  glb and streams all six manifest assets through the async lane with
  zero errors and zero fallback warnings; warm run serves both from the
  cooked cache without invoking an importer.
- Deliberately not done, with reasons: no node-transform baking (the
  scene places instances; the cook emits geometry as authored), no
  material/skin/animation extraction from glTF (materials are authored
  `.smat`; skins/clips are Stage 5, where the multi-artifact keying
  built in 4a starts paying), and glTF primitives without NORMAL are
  rejected rather than computed — re-export is cheap, silent normal
  generation is a data-quality lie.

### Stage 4d — audio (landed)

Test-verified (799 tests green; 17 new across `test/core/AudioAssetTests.cpp`
and the preload suite, TSan-clean). Decision F made real — and the
extensibility proof came in cheap: the whole type is one container, one
loader, one importer, and switch-case additions; no contract needed bending.

- `assets/audio_clip/AudioClipFormat.h` + `AudioClipSerializer.{h,cpp}` —
  the `.sclip` container: fixed header (magic, version, rate, channels,
  sample count) + interleaved Sint16 PCM. Decode happens at cook; the
  runtime parse is a validated copy, which is what makes the audio stage
  half pure. Read/write halves are pure functions; bounds checks compare
  counts, not byte products, so a crafted header can't overflow past them.
  The container carries *clips* only — the Decision F streamed-music
  boundary is restated in the header so it doesn't erode silently.
- `AudioClipAssetLoader` — the staged contract, and the first loader whose
  round trip is fully headless (no GPU half): stage sniffs container magic
  (cooked artifacts keep their `.wav` virtual paths, the 4b precedent) and
  falls back to in-memory WAV decode for loose bytes; commit registers
  with `AudioClipCache`.
- `AudioClipCache` lost its file IO (Decision I): `OnLoad` no longer
  touches the filesystem — `Acquire` resolves registered entries only, so
  it doubles as the never-loads `TryAcquire`. `Register`/`Find`/`GetName`
  joined the surface; `LoadAudioClipFromFile` became
  `LoadAudioClipFromWavBytes` (bytes in, clip out — shared by the runtime
  fallback and the WAV importer).
- `AssetSystem` gained `LoadAudioClip`/`TryAcquireAudioClip`/
  `ReleaseAudioClip` and routes `AssetType::Audio` through `LoaderFor`;
  `RuntimeAssets` owns an `AudioClipCache`; the preloader carries clips in
  **wave 1** (audio is leaf data — nothing resolves refs against it at
  commit); the scanner maps `.sclip` → Audio.
- `assets/cook/AudioCook.{h,cpp}` (SENCHA_ENABLE_COOK only) — one
  `AudioClipImporter` for `.wav` and `.ogg`, emitting `.sclip` under the
  source's virtual path. OGG decode is stb_vorbis (single-TU inclusion
  from the already-pinned stb checkout — no new dependency); WAV decode is
  the shared SDL path. No resampling: clips keep the source rate and
  `AudioService` resamples on playback, as before.
- AudioTest is the end-to-end proof, migrated to the front door: cook
  `assets/sampleSound.wav` → scan → `AssetSystem::LoadAudioClip` →
  playback through the existing cache `Get`. Verified headless (dummy SDL
  drivers): cold run cooks one artifact; warm run serves from the cooked
  cache without invoking the importer.
- Gate honesty: the OGG fixture is a committed 1024-frame sine encoded
  once with libvorbis (`test/core/AudioOggFixture.h`) — vorbis is lossy,
  so its tests pin rate/channels/duration and waveform amplitude, never
  exact samples; the WAV path *is* byte-exact and tested as such. No
  scene component references audio yet, so manifest-driven audio preload
  is exercised by direct preloader tests rather than a demo scene — the
  wave-1 path is pinned headless alongside the existing suite.

The original open questions were answered the day the plan was written:

1. **Mesh sources: glTF 2.0 and `.blend`** — `.blend` via headless Blender in
   the cook step (dev-only dependency); runtime format stays our own
   `.smesh`. Folded into Decision B.
2. **Editor: Phase 1 is in development on a separate branch.** `AssetId`
   groundwork moved into Stage 4; id scheme to be coordinated with that
   branch. Folded into Decision A.
3. **Animation: in scope now**, asset-side (formats, import, caches); the
   animation *runtime* remains a separate future plan. New Decision J,
   new Stage 5.
4. **`.stex`: plan for compression from day 1 without hardcoding anything** —
   format-tagged container, no consumer may assume uncompressed layout, BC
   fixture in tests from the first commit. Folded into Decision E.

Still open, neither blocking Stages 1–3: the glTF parser library (cgltf
proposed — implementation detail), and everything Decision J explicitly
defers to the animation runtime plan.

## Product input record, second pass (2026-06-11)

A follow-up review asked for more depth on the texture and skeletal-asset
story. Four calls:

1. **Scope: cross into runtime where the formats force it.** The doc now
   sketches the GPU skinning seam (Decision N) and the material/shader
   implications (Decision L) — sketches, not commitments; the render ladder
   and animation runtime remain separate plans.
2. **Textures: plan the full PBR set now.** Usage-tagged `.stex`, cook-side
   mips, BC5 normals, packed ORM, and the PBR `.smat` schema — Decision L.
   Stage 1 still ships base-color-only but writes the L schema, so no
   content is ever authored twice.
3. **Vertex formats: decided now, not reserved.** Tangents in the base
   vertex (Stage 4), skinning as a separate stream (Stage 5) — Decision M.
   Vertex color and UV1 stay reserved — they have no customer.
4. **Skinning direction: kept genuinely open.** Decision N records both
   candidates, the deciding inputs, and the asset-side guarantees that hold
   under either — so the choice can be made late without re-cooking
   anything.
