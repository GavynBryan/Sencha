# Document Cook Pipeline Refactor

Status: in progress.

- Stages 0-4, 7, and 8 are landed on `lightmap-spike`. `CookDocumentKernel` no
  longer exists: `ExecuteDocumentCook` is a readable orchestrator over concrete
  mechanisms (`CookStepProgress`, `DocumentCookPaths`, `DocumentBakeOcclusion`,
  `DocumentCookReuse`, `LightmapSurfaceCook`, `CellArtifactCook`,
  `DocumentLightmapBake`, `DocumentProbeBake`, `CookedSceneAssembly`,
  `DocumentPublication`). `DocumentCook.cpp` is 325 lines; the mechanisms carry
  focused tests and the suite is green.
- Stage 8 was landed as a behavior-preserving structural decomposition ahead of
  Stages 5 and 6. The extracted seams hold the current (fused) behavior: the
  lightmap bake still resolves direct and AO together behind
  `BakeDocumentLightmap`, and publication is still a single-hash cache decision
  plus `StageDocumentIndex`/`StageDocumentReceipt` rather than a
  `DocumentPublicationPlan`.
- Stages 5 (engine lightmap surface-sample split with independent direct/AO
  cache) and 6 (cumulative publication plan) remain. They are the two intended
  behavior changes; each now slots behind an established seam rather than into
  the monolith. Two disabled characterizations pin them:
  `DocumentCookFingerprints.DirectLightMovesDirectNotSurfaces` asserts the
  current direct/AO coupling (flip to `EXPECT_EQ` at Stage 5), and
  `DISABLED_PreserveLightingKeepsSceneReferencingTheAtlas` awaits Stage 6.

Scope: the editor-side document cook beginning in
[`DocumentCook.cpp`](../../editor/kyusu/src/document/DocumentCook.cpp), the
engine lightmap raster contract it consumes, and the artifact publication path.
This plan does not change the editor profile UI or runtime asset-loading model
except where their contracts must remain truthful.

## Outcome

Replace `CookDocumentKernel` with a short, explicit orchestrator over concrete
cook mechanisms. Cooking remains one deterministic pipeline selected by profile
data. Every selected step has truthful dependencies, fingerprints, cache reuse,
progress, cancellation, receipt data, and publication behavior.

The intended flow is:

```text
immutable snapshot + resolved request
                |
                v
       graph-derived identities
                |
                v
      reusable step resolution
                |
                v
   cell geometry + lightmap surfaces
                |
                v
          occlusion geometry
                |
                +--------+--------+
                |        |        |
                v        v        v
             direct      AO     probes
                |        |        |
                +--------+--------+
                         |
                         v
             publication selection
                         |
                         v
           scene and artifact staging
                         |
                         v
                  atomic commit
```

The target is not a generic pipeline framework. The cook has one concrete
execution model, so the orchestration should stay closed and readable.

## Current diagnosis

`CookDocumentKernel` currently owns all of these mechanisms:

- progress and cancellation
- path derivation
- input fingerprint construction
- whole-document and per-step cache reuse
- brush clustering
- lightmap atlas layout and UV mutation
- render mesh construction
- collision construction
- occlusion triangle collection and BVH construction
- direct-light evaluation
- ambient-occlusion evaluation
- irradiance-probe evaluation
- scene assembly
- referenced-asset importing
- cache-index mutation
- receipt construction
- artifact staging and transaction commit

This is not only a file-organization problem. Several surrounding contracts now
describe behavior that the implementation does not provide.

### The graph is advisory

[`CookGraph.cpp`](../../editor/kyusu/src/document/CookGraph.cpp) resolves selected
steps and prerequisites, but the kernel does not execute from that result.
Branches based on input presence, output policy, prior artifacts, and fallback
requirements independently decide what runs. Progress is then forced to a final
step count even when the actual begin and complete events did not match the
resolved graph.

### Declared dependencies are incomplete

The kernel writes atlas UVs into cell vertices before mesh serialization and
writes placement scale/bias into the passthrough scene, so render meshes and
the cooked scene both depend on the atlas layout. The graph declares only
`render_meshes` after `brush_cells` and `cooked_scene` after `render_meshes`.
Nothing breaks today because meshes and the scene are never per-step cached and
the document identity folds the lightmap parameters, but once the graph drives
reuse these edges must be stated or a luxel-size change serves stale meshes.

### Direct light and AO are falsely independent

The graph and receipt model present direct light and AO as separate work. The
current [`LightmapRaster`](../../engine/src/assets/cook/LightmapRaster.cpp)
operation resolves surface samples and evaluates both in one call. Reuse is
therefore all-or-nothing, and an AO setting change can needlessly recompute
direct lighting.

AO currently depends on the direct-light step in the graph. Its mechanical
inputs are lightmap surfaces and occlusion geometry. It does not depend on the
direct-light texture.

### Fingerprints restate dependencies manually

The kernel repeatedly hashes the same source collections. The document identity
also restates raw inputs already represented by step identities. Receipt
dependencies, cache lookup, and whole-document reuse can drift because they do
not consume one dependency model.

Referenced-asset freshness must also be audited. A source material or texture
change must not be hidden by a document-level cache hit merely because its asset
path stayed unchanged.

### Artifact ownership is distributed

Public artifacts, material references, generated mesh paths, optional lighting
outputs, and restored cache artifacts live in parallel containers.
`RestoreCookStepArtifacts` mutates several caller-owned containers and takes a
boolean that changes registration semantics. No value owns the invariant that a
scene-referenced output, its receipt entry, and its public artifact record agree.

### Publication policy is applied too late and incompletely

Publish, Preserve, and Withdraw are output-family decisions. The kernel applies
some of them only while writing files or receipts. A preserved artifact can
remain on disk while disappearing from the newly assembled scene, which is an
effective withdrawal for consumers.

### Referenced-asset import is outside the document transaction

On-demand importing may publish cooked assets and mutate the global cache index
before document publication commits. A later document failure can leave those
changes active even though the document cook failed.

## Architectural constraints

The refactor must preserve these constraints from `CLAUDE.md`:

- Files and types are named for mechanical responsibilities.
- Each file owns one tight mechanism.
- Concrete values and functions are the default inside the editor cook layer.
- Profiles remain data. Built-in and project profiles do not create separate
  pipelines.
- The engine cook layer never depends on editor document types.
- The cook remains gated by `SENCHA_ENABLE_COOK` and does not enter the runtime.
- Existing job-system and async-task lanes remain the only concurrency lanes.
- The serial path is the deterministic reference.
- A failed or cancelled cook cannot partially replace active publication.

Do not introduce `ICookStep`, step factories, a service locator, a generic
pipeline registry, or a mutable context object that every operation can reach
through. Those shapes do not protect a present boundary or variation axis.

## Target contracts

Names below are working mechanical names. During implementation, use the
existing vocabulary when it already expresses the mechanism more precisely.

### `DocumentCookSnapshot`

An immutable value containing captured authored and resolved asset data. It
replaces the data-bearing portion of `DocumentCookInput::Data`.

It contains geometry, placements, lights, probe volumes, halo geometry,
lighting parameters, passthrough scene data, and source identities. Collection
may skip gathering inputs no step in the resolved graph consumes (a
collision-only cook builds no charts and loads no placement geometry), but
execution never infers selection from data absence: what runs is decided by the
request and graph, and an empty input to a selected step is a valid state, not
a disable signal.

### `DocumentCookRequest`

A value containing the selected profile, resolved graph, output dispositions,
force-rebuild flag, output namespace, and cancellation/progress control.

This separates the question "what is available to cook?" from "what did the
caller request?" The existing `RunDirectLightmap`, `RunAmbientOcclusion`, and
similar booleans should disappear once execution is graph-driven.

### `DocumentCookFingerprints`

A concrete value containing one named fingerprint per graph step plus the
passthrough scene and final publication identities.

Construction follows graph dependencies:

```text
step identity = step version
              + direct authored inputs
              + dependency step identities
```

The final document identity composes step and publication identities rather
than hashing the same authored values again. One function constructs this value
before cache resolution. Execution code does not calculate hashes.

Internal step renames or dependency changes must bump the affected step version.
Persisted profile-selectable step IDs remain stable unless an explicit profile
migration is included.

### `DocumentCookPaths`

A plain value containing every authored, staged, cache, and active-publication
path derived for one document cook. Path derivation happens once and is tested
independently. Cook operations receive only the paths they use rather than
reconstructing naming rules locally.

### `DocumentArtifactCatalog`

A concrete owner for artifacts known during one cook. It records:

- artifacts restored from prior receipts
- artifacts produced during this invocation
- public output family ownership
- generated assets referenced by the assembled scene
- material and ID-map references
- typed paths for collision, direct light, AO, and probes

Registration operations state their semantics directly. A boolean such as
`sceneReference` is not part of the API.

The catalog does not write files and does not decide profile policy. It owns
artifact identity and membership invariants only.

### `CookStepProgress`

A small concrete operation around the existing `CookControl`. It is initialized
from the resolved graph and records:

- selected step order
- current step
- cancellation checks
- start and completion events
- elapsed duration
- generated or reused outcome

Only this operation updates progress. A step cannot complete unless it began,
and a reused step has an explicit reused outcome. Step duration is copied into
`CookStepReceipt::DurationMilliseconds`.

Long engine operations receive the existing cancellation mechanism or a narrow
progress callback only where work is actually divisible. No new threading
mechanism is introduced.

### `DocumentPublicationPlan`

A value resolved before scene assembly. For every public output family it
selects one state:

- produced by this cook
- restored from a reusable step
- preserved from the active publication
- withdrawn

The plan validates that every produced, restored, or preserved output exists
and matches its receipt. Scene assembly, receipt construction, manifest
construction, and file withdrawal all consume this same value.

Preserve means the new scene and receipt continue to reference the prior active
output. It does not merely mean "leave the old file on disk."

### Lightmap surface samples

The engine cook layer gains a typed intermediate for stable lightmap surface
coverage. The exact name should match the data after the first extraction, but
its contract is:

- atlas coordinates and resolved surface samples are calculated once
- deterministic dilation and coverage data are shared
- direct-light evaluation consumes the sample map and occlusion queries
- AO evaluation consumes the same sample map and occlusion queries
- neither output evaluation owns atlas layout
- direct and AO can run, cancel, fingerprint, and cache independently

This is an engine-level contract because both the data and algorithms are
independent of editor document types.

### Shared geometry values

Brush clustering should produce a CPU-side cell value that can feed render mesh
and collision emission independently. Transformed triangle gathering from
`MeshGeometry` should be shared by document cooking and zone-halo collection.

Keep brush-face and placed-mesh conversion as separate concrete functions. They
have different inputs and invariants even if both eventually append triangles.

### Referenced-asset preparation and publication

The engine importer currently fuses two operations in `ImportAssetsOnDemand`:
discovering and cooking stale sources, and publishing the results into the
active cooked asset set and index. Split them so the document cook can prepare
imports without touching active state and publish them through its own
transaction. This is a generic engine-cook capability, not a document concept.

Three side effects are separated here, not two: producing cooked bytes and index
entries, publishing them into an active cooked tree and index, and registering
them into a live `AssetRegistry`. Fusing registration with production is what
lets a failed publish leave the registry pointing at assets that never landed, so
registration becomes an explicit last step over the produced records rather than
a side effect of preparation.

The importer already owns a write seam, `ICookOutputWriter::WriteBytes`
(`AssetImporter.h`). Extend it, do not replace it, with a publisher that also
owns index entries so bytes and index membership have one owner:

```cpp
class IImportPublisher : public ICookOutputWriter   // inherits WriteBytes
{
public:
    // Upsert one source entry into the index this publisher owns.
    virtual void PutIndexEntry(CookedSourceEntry entry) = 0;
};
```

Preparation, publication, and registration become explicit operations:

```cpp
struct CookedCacheIndexDelta { std::vector<CookedSourceEntry> Puts; };  // never erases

struct PreparedCookedArtifact {
    CookedArtifact        Artifact;      // final FileRelPath and resolved hash
    std::filesystem::path PreparedFile;  // private temp staging path
};

// Move-only, pure plan value. Owns a temp staging directory; RAII removes it if
// unpublished. Names no registry and no transaction.
class PendingAssetImport {
    std::vector<PreparedCookedArtifact> Artifacts;     // stale/new sources, staged
    CookedCacheIndexDelta               IndexDelta;     // new plus upgraded fresh entries
    std::vector<CookedArtifact>         Registrations;  // every fresh and imported record
    ImportOnDemandStats                 Stats;
};

// Pure producer. Reads the active index and artifact files read-only; writes
// nothing active and touches no registry.
[[nodiscard]] bool PrepareAssetsOnDemand(
    const std::filesystem::path& assetsRoot, const AssetImporterRegistry& importers,
    LoggingProvider& logging, PendingAssetImport& out);

// Applies bytes and index entries through one owner. Runs no scan, no importer.
[[nodiscard]] bool PublishAssetImport(
    PendingAssetImport&& pending, IImportPublisher& publisher, std::string* error);

// Registers produced records into a live registry after a successful publish,
// so a failed publish registers nothing. Takes records, not the pending value:
// publication consumes the pending by move, so callers copy `Registrations`
// (and `Stats`) out first. Registration is a pure apply over records; it needs
// neither the temp files nor the delta.
[[nodiscard]] bool RegisterImportedAssets(
    std::span<const CookedArtifact> records, const std::filesystem::path& assetsRoot,
    AssetRegistry& registry, LoggingProvider& logging);
```

The tree has no `Expected` type, so results travel through `bool` and out
parameters, matching the existing importer idiom. Per-source failures stay
advisory (logged, counted in `Stats`); preparation still returns the successful
subset. Bytes stage to temporary files, not memory, because the importers
already stream through the writer rather than buffering.

Preparation is a pure plan producer. It:

- loads the active index read-only for freshness checks;
- resolves every artifact content hash up front: stamped from the writer for new
  imports, read once from the active file for a pre-hash fresh entry, so both the
  index delta and the registration records carry a known hash and no later step
  reads an artifact back;
- runs importers for stale or new sources against a private temp-staging writer;
- records each fresh and imported artifact in `Registrations`, each new or
  hash/stat-upgraded source in `IndexDelta`, and each imported file in
  `Artifacts`;
- writes no active file, writes no active index, and mutates no registry.

Publication is all-or-nothing against a publisher-owned staging and index. It:

- validates that no destination `fileRelPath` is claimed twice before writing any
  bytes;
- forwards each prepared file's bytes through `publisher.WriteBytes`, one
  artifact at a time;
- applies each delta entry through `publisher.PutIndexEntry`. Committing the
  index is not publication's job: the publisher's owner commits as its final
  act (`ImportAssetsOnDemand` saves the filesystem writer's index; the document
  cook calls `CookArtifactTransaction::Commit`), after every byte write
  succeeds, so a failure leaves index membership, and therefore the import set,
  unchanged;
- runs no scan, no importer, and no cooking logic.

Two publishers implement the seam:

- `FilesystemImportArtifactWriter` writes to the active `.cooked/` tree and owns
  its own `CookedCacheIndex`. It saves that index once, last, through a
  temp-and-rename, so set membership flips atomically even though byte files land
  in place. A mid-write byte failure can leave an inert unreferenced file under
  `.cooked/`, never a referenced-but-missing artifact.
  `CookedCacheIndex::SaveToFile` truncates in place today and must gain the
  temp-and-rename that `SaveDocumentCookReceipt` already uses.
- a document-transaction publisher (editor-side, since it knows
  `CookArtifactTransaction`) stages bytes through the transaction and puts index
  entries into the document cook's single staged index. Byte files and index then
  commit together, so this publisher is fully atomic, not only
  membership-atomic.

The engine importer gains no document-cook or transaction concept: the
transaction publisher lives in the editor and depends on the engine seam, never
the reverse. The index has one owner during a composed publication;
`PublishAssetImport` applies its delta to that owned index and never reloads or
saves `index.json` itself. Import entries key on their source paths (the `.png`,
`.gltf`, and so on) while the document entry keys on the level source path, so
the two never collide in the shared index. The delta only upserts; pruning an
orphaned artifact left by a changed output set stays the existing separate
concern.

## Proposed file boundaries

The split is by mechanism, not by arbitrary function size:

- `DocumentCook.cpp`
  - public `CookDocument` overloads
  - `ExecuteDocumentCook`
  - short deterministic orchestration only
- `DocumentCookInput.cpp`
  - capture live or saved document state into `DocumentCookSnapshot`
- `DocumentCookFingerprints.h/.cpp`
  - named step identity construction
- `DocumentCookPaths.h/.cpp`
  - deterministic source, cache, staging, and publication paths
- `DocumentArtifactCatalog.h/.cpp`
  - artifact membership and typed output lookup
- `CookStepProgress.h/.cpp`
  - graph-aware progress, cancellation, and timing
- `CellGeometryCook.h/.cpp`
  - brush clustering and reusable CPU cell geometry
- `LightmapSurfaceCook.h/.cpp`
  - document adapters for atlas layout, brush UV assignment, and engine sample
    inputs
- `BakeTriangleGather.h/.cpp`
  - transformed mesh and brush triangle gathering
- `DocumentLightmapBake.h/.cpp`
  - editor-side direct and AO step execution and artifact serialization
- `DocumentProbeBake.h/.cpp`
  - probe step execution and artifact serialization
- `DocumentPublicationPlan.h/.cpp`
  - output-family policy resolution and validation
- `CookedSceneAssembly.h/.cpp`
  - scene JSON and manifest construction from selected outputs
- `DocumentImportPublisher.h/.cpp`
  - the `IImportPublisher` that stages referenced-asset imports through
    `CookArtifactTransaction` into the document cook's staged cooked index
- existing `CookReceipt.h/.cpp`
  - receipt values and deterministic serialization
- existing `CookArtifactTransaction.h/.cpp`
  - staging and atomic commit of the resolved publication

The final names may be fewer if adjacent operations share one invariant and
lifecycle. Do not create one-file-per-function noise. Output policy resolution,
scene assembly, receipt serialization, and transaction commit stay separate
because they have different inputs, failure modes, and tests.

The engine-side lightmap split stays beside the existing cook implementation:

- `LightmapSurfaceSamples.h/.cpp`
  - stable sample-map construction and deterministic coverage
- `DirectLightmapBake.h/.cpp`
  - direct evaluation over the sample map
- `AmbientOcclusionBake.h/.cpp`
  - AO evaluation over the sample map

If the current types can express one of these mechanisms cleanly after
extraction, evolve them instead of adding parallel APIs.

The engine importer split stays in the existing importer files:

- `assets/cook/ImportOnDemand.h/.cpp`
  - `PrepareAssetsOnDemand`, `PublishAssetImport`, `RegisterImportedAssets`, and
    the `ImportAssetsOnDemand` wrapper over prepare, filesystem publish, register
- `assets/cook/AssetImporter.h` (or a small sibling)
  - `IImportPublisher` extending the existing `ICookOutputWriter`, and
    `FilesystemImportArtifactWriter`

## Execution stages

### Stage 0: Characterize the current contract

Add tests before moving behavior. Record baseline outputs from representative
documents with brushes, placed meshes, collision, direct light, AO, probes, and
referenced assets.

Required coverage:

- Full cook from a clean cache produces the expected artifact set.
- Repeating Full is a whole-document cache hit with byte-identical outputs.
- Lighting Only preserves structure and collision while replacing requested
  lighting outputs.
- No Lighting replaces structure and collision while withdrawing or preserving
  lighting exactly as its profile declares.
- Custom profile Publish, Preserve, and Withdraw behavior is pinned for every
  output family.
- Cancellation during each long step leaves active publication unchanged.
- A staging or commit failure leaves active publication and index unchanged.
- Serial runs are byte-identical and emit stable receipt ordering.
- Progress events correspond to work that actually ran.
- A changed referenced material or texture invalidates the required import even
  when its asset path is unchanged.
- Output-namespace stability: a world cook publishes under a content-addressed
  stem inside `_world-generations/`, and the world manifest references those
  paths, so an identity recomputation that changes the stem must be a
  deliberate tested invalidation, never an accident.
- Placement entities receive the expected `lightmap_scale_bias` values for a
  pinned atlas layout.
- Cached-step restoration resolves its outputs without relying on artifact
  ordering; the current code takes the last appended artifact as the restored
  output.

Gate: tests reproduce current intended behavior and expose known policy or
progress defects as explicit failing or disabled cases with the desired result
documented in the test name.

### Stage 1: Separate captured data from execution policy

Introduce `DocumentCookSnapshot` and `DocumentCookRequest`. Keep
`DocumentCookInput` as the move-only public envelope if that preserves the
existing background-session boundary cleanly.

Collection captures complete inputs needed by the resolved graph. It does not
erase lights, charts, probes, collision data, or references to signal disabled
steps. Resolve profile selection once into the request.

Gate:

- Collection tests demonstrate that equal documents under equal resolved graph
  requirements produce equal snapshots, and that collection stays lazy where
  the graph requires nothing (a collision-only cook builds no charts and loads
  no placement geometry).
- Execution uses graph membership rather than `Run*` booleans.
- No runtime or renderer dependency enters the snapshot types.

### Stage 2: Centralize identities and cache decisions

Add `DocumentCookFingerprints`. Give every graph node an explicit direct-input
identity function and compose dependencies in resolved order.

Update `CookStepCache` so reusable lookup consumes a step definition and its
precomputed identity. Consolidate repeated receipt construction into concrete
operations for restoring a reusable step and recording a produced step.

Audit referenced-asset identity. The resolution is Stage 7's prepare-first
rule: `PrepareAssetsOnDemand` runs before the whole-document fast-path
decision (stat checks when everything is fresh), and a non-empty delta
invalidates only the referenced-assets family. Do not duplicate the asset
pipeline's own content hash; the delta is the freshness signal.

Gate:

- Fingerprint dependency tests show that changing one input invalidates exactly
  the steps that mechanically depend on it.
- Execution contains no local hash construction.
- Missing step definitions produce a local error instead of a null dereference.
- Existing receipts either migrate deliberately or invalidate through version
  changes.

### Stage 3: Establish progress and artifact ownership

Introduce `CookStepProgress` and `DocumentArtifactCatalog`. Route all step
begin, reuse, completion, cancellation, and timing through the progress
operation. Route all artifact restoration and registration through the catalog.

This stage should be behavior-preserving apart from fixing inaccurate progress
and filling receipt durations.

Gate:

- No direct progress-counter mutation remains in the orchestrator.
- A step cannot complete twice or complete without beginning.
- Reused steps are distinguishable from generated steps.
- Cancellation checks occur inside long direct, AO, and probe loops at stable
  deterministic boundaries.
- Artifact restoration no longer mutates multiple parallel containers.

### Stage 4: Extract geometry mechanisms

Extract brush clustering and CPU cell geometry before splitting render mesh and
collision emission. Extract transformed triangle gathering and use it from both
occlusion construction and zone-halo collection.

Isolate document-to-lightmap-surface adaptation, atlas placement application,
and brush UV application from evaluation.

This is a structural stage. Generated bytes and cache identities should remain
unchanged unless a previously untracked dependency is corrected and versioned.

Gate:

- Render mesh and collision are independent graph operations over shared cell
  geometry.
- Occlusion and halo code share transformed triangle gathering.
- Brush and placement adapters remain concrete and separately testable.
- Baseline geometry, scene JSON, collision, and atlas outputs remain identical.

### Stage 5: Split the lightmap backend

Rewrite the engine lightmap contract around the stable surface sample map.
Separate direct-light and AO evaluation. Both depend on lightmap surfaces and
occlusion geometry; neither depends on the other's output.

Update graph definitions, step versions, fingerprints, receipts, progress, and
cache reuse together. Remove the special dependency injection in
`ResolveDocumentCookGraph`; dependencies should be fully stated by each step
definition. State the missing edges while doing so: `render_meshes` and
`cooked_scene` depend on `lightmap_surfaces` whenever surfaces are selected,
because atlas UVs enter mesh vertices and placement scale/bias enters the
scene.

Define empty-input behavior explicitly:

- Direct light with surfaces and no bake lights emits no direct artifact.
  Runtime sampling already defines a missing direct atlas as zero contribution,
  so a selected Publish replaces any prior direct output with absence.
- AO does not require any direct lights to exist.
- A profile selecting AO without direct light remains valid if its mechanical
  prerequisites are present.
- AO-only publication changes the runtime `ZoneLightmap` contract: today the
  component carries a required direct texture and an optional AO plane, so the
  loader and shader must accept an AO plane with no direct texture. Verify and
  land that runtime change with this stage.
- Collection keys lightmap-surface inputs (charts, placements, atlas UVs) on
  whether the resolved graph requires surfaces, not on whether bake lights
  exist; today charts are gathered only when direct lights are present.

Gate:

- Changing only AO parameters reuses direct light.
- Changing only direct-light inputs does not invalidate AO.
- Changing surface layout or occlusion invalidates both.
- AO-only execution performs real AO work without requiring a direct-light
  target.
- Cancellation and progress identify the exact evaluation being performed.
- Serial output stays deterministic. Any intended byte change is covered by a
  version bump and updated golden result.

### Stage 6: Make cumulative publication explicit

Build `DocumentPublicationPlan` from the request, produced artifacts, reusable
artifacts, and active receipt before assembling the scene.

Apply the plan uniformly to structure, collision, referenced assets, direct
light, AO, and irradiance probes. Specify the whole-document fast path against
this plan before implementing the stage: a no-op hit is a publication plan in
which every family resolves to preserve or restore with its artifacts
validated, plus an empty referenced-asset delta. The single-hash comparison
the kernel performs today is not sufficient once publication is cumulative.

Scene assembly consumes selected outputs from the plan, so preserved outputs
remain referenced. Withdrawal removes references and stages removal only for
artifacts owned by the withdrawn family.

Gate:

- Every output family passes a Publish, Preserve, and Withdraw matrix test.
- Sequential Full, Lighting Only, and No Lighting cooks produce the expected
  cumulative scene regardless of starting profile.
- Preserved output is both present and referenced in the new publication.
- Withdrawn output is absent from scene references and receipt publication
  state.
- A missing preserved artifact fails before commit with a precise diagnostic.

### Stage 7: Separate referenced-asset preparation from publication

This is an independent engine change. It does not depend on the lightmap backend
rewrite or the kernel extraction, and can land on its own, because the document
cook already owns a transaction and a single staged cooked index. Do not combine
it with Stage 5 or Stage 8 in one commit.

Introduce `PrepareAssetsOnDemand`, `PublishAssetImport`, `RegisterImportedAssets`,
the `IImportPublisher` seam, and the two publishers described under
"Referenced-asset preparation and publication." Keep `ImportAssetsOnDemand` as a
thin wrapper: prepare, copy the registration records and stats out (publication
consumes the pending value by move), publish through the filesystem publisher,
then register.
Reuse the existing `ICookOutputWriter` seam and the file-local
`FileCookOutputWriter` write-and-stamp pattern for the temp-staging writer. Give
`CookedCacheIndex::SaveToFile` the temp-and-rename that the receipt writer uses.

Rewire the document cook: prepare referenced-asset imports early without changing
active state, then publish them through the document-transaction publisher into
the same staged index that receives the document's own source entry. Reject a
`fileRelPath` claimed by both an import and a generated document artifact, since
the transaction would otherwise coalesce two writers onto one staged file. The
document cook does not register: its scratch registry is gone with the rest of
the fused call. Preparation runs before the whole-document fast-path decision;
a non-empty delta invalidates only the referenced-assets family. Prepare
failures stay advisory in the composed flow, as the discarded return of the
fused call is today: the cook publishes the successful subset and logs the
rest. The transaction then commits imported assets, generated document
artifacts, scene, manifest, collision sidecar, AssetId map, cooked index,
withdrawals, and receipt as one change.

Gate:

- `PrepareAssetsOnDemand` modifies no active cooked file, no active index, and no
  registry.
- Abandoning a `PendingAssetImport` cleans its temporary output and leaves active
  state untouched.
- Standalone `ImportAssetsOnDemand` remains behavior-identical for non-document
  consumers, including registry population.
- Prepared imports publish through a caller-owned transaction.
- A document cook that fails after preparation leaves imported assets and the
  active index unchanged.
- A successful composed cook commits imported assets and the document source
  entry through one staged index.
- Import index entries and the document index entry are written through that one
  staged index, never through an independent `index.json` save inside publish.
- Conflicting destination paths are detected before any bytes are written.
- A publication failure leaves the active import set unchanged: fully through the
  transaction, and at index-membership granularity through the filesystem
  publisher's index-last commit.
- A failed publish registers nothing.
- A stale referenced source defeats the whole-document fast path for the
  referenced-assets family only.
- Prepare failures in the composed flow stay advisory: the successful subset
  publishes.
- `PublishAssetImport` executes no importer.

### Stage 8: Reduce the kernel to orchestration

Once the contracts above are live, delete `CookDocumentKernel`. Keep
`ExecuteDocumentCook` as the explicit sequence of named operations and early
failure propagation.

The function should read as orchestration:

```text
validate request
build identities
resolve reuse
execute selected operations
resolve publication
stage publication
commit
return result
```

Do not optimize for the smallest possible line count. A target of roughly 100
to 200 lines is useful only as a scanability check. The real gate is that no
geometry, lighting, serialization, cache-format, or transaction algorithm lives
inside the orchestrator.

Gate:

- `DocumentCook.cpp` contains facade and wiring logic only.
- Each extracted file owns one tight mechanism and its private functions.
- No new interface has only one implementation without a real boundary.
- Full level-cook and editor test suites pass.
- A clean-cache cook and repeated cached cook are profiled to confirm that the
  refactor did not introduce a material regression.

## Test organization

Tests should be grouped by contract rather than collected in one kernel test:

- `DocumentCookFingerprintTests.cpp`
- `CookGraphExecutionTests.cpp`
- `DocumentArtifactCatalogTests.cpp`
- `DocumentPublicationPlanTests.cpp`
- `LightmapSurfaceSamplesTests.cpp`
- `DirectLightmapBakeTests.cpp`
- `AmbientOcclusionBakeTests.cpp`
- `DocumentCookIntegrationTests.cpp`
- `DocumentCookCancellationTests.cpp`
- `AssetImportPublicationTests.cpp`

Use in-memory or temporary-directory writers at the existing asset-pipeline and
publication boundaries. Do not create an interface solely to unit-test a pure
value or free function.

The integration fixture should support fault injection at staging and commit
boundaries. That is a justified test seam because transaction correctness
cannot otherwise be exercised without manipulating the real asset tree.

## Migration discipline

Each stage should land with the suite green and with old code removed as soon as
its replacement is active. Do not maintain parallel old and new cook paths.

For serialized receipts and cached step artifacts:

1. Keep stable public profile IDs and selectable step IDs.
2. Bump a step version whenever its identity inputs, dependency contract, or
   artifact format changes.
3. Treat old internal-step receipts as non-reusable when their dependency model
   changes.
4. Prefer safe cache invalidation over a compatibility adapter that complicates
   every future cook.
5. Keep active publication readable until a new complete publication commits.

No stage should combine broad file movement with an untested output-format
change. Structural extraction and intentional behavior changes should be
separate commits where practical.

## Deliberate non-abstractions

The following repetition should not automatically be generalized:

- Brush-face and placed-mesh lightmap adapters have different source
  invariants.
- Direct-light, AO, and probe artifact formats remain separately named
  serialization operations.
- The closed document cook order does not need a registered plugin system.
- A small local mapping from stable step ID to a concrete operation is
  acceptable if it remains wiring only.

The repetition that should be consolidated is repetition of a real contract:

- transformed triangle gathering
- lightmap surface sample resolution
- step identity composition
- reusable artifact restoration
- produced-step receipt recording
- progress and cancellation accounting
- output-family publication selection
- transaction-owned cache-index staging

## Completion criteria

The refactor is complete when all of the following are true:

- `CookDocumentKernel` no longer exists.
- `ExecuteDocumentCook` is a readable deterministic orchestrator.
- The resolved graph controls execution, progress, cache reuse, and receipts.
- Direct light and AO share surface sampling but evaluate and cache
  independently.
- Profiles remain data-driven and cumulative across sequential cooks.
- Publish, Preserve, and Withdraw have one definition used by scene assembly,
  receipts, and artifact ownership.
- All document and referenced-asset outputs commit atomically.
- A cancellation or failure leaves active publication untouched.
- Fingerprints have explicit dependency tests.
- The serial path remains deterministic.
- Extracted mechanisms have focused tests and no speculative interface layer.
