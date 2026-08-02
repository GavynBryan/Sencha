# Movement, Asset Orchestration, Structured Data, and Data Authoring Plan

Status: shipped. Recorded below is what landed, including where the
implementation deviates from this plan; the sections that follow are the
original design and are kept for the reasoning, not as a description of the
tree.

## What shipped, and where it differs from this plan

Everything here landed on top of the unified-runtime-world and asset-layer
work that arrived on main while this was in flight, which changed several
answers:

- **No `AssetCommitResult`, and no type-erased `Commit` on the stager.** Main
  had already reduced `IAssetLoader` to `IAssetStager` with `LoadStaged` as the
  only virtual, on the grounds that a type-erased commit could only lose the
  loader's own handle type. `AssetLease` answers that objection (token plus
  issuing `ILifetimeOwner` is exactly what a typed handle carries), so the
  generic commit came back as a *registered operation on the kind* rather than
  a virtual on the stager. `IAssetStager` is untouched.
- **One extension table.** `MakeBuiltinAssetKind` is the only place a runtime
  extension is named; `ScanAssetsDirectory` takes the kind registry to classify
  against. The plan's fallback that re-declared the table for callers without
  an `AssetSystem` was dropped in favour of `BuiltinAssetKindRegistry`.
- **Preload ordering is fully declared.** `AssetStaging::Dependencies` replaced
  the two-wave leaf-then-material schedule outright, which also let skeletons,
  animation clips, and skinned meshes onto the async lane: the double-release
  that kept them off it is structurally impossible once a commit declares what
  it needs instead of inline-loading it.
- **The registry-qualification work was dropped, not ported.** Attaching
  `EntityRef` to physics bodies existed because each zone owned a registry and
  `EntityId` was ambiguous across them. Under the unified World there is one
  entity space, so `EntityId` is already unambiguous and `EntityRef` no longer
  exists.
- **No migration tier.** The plan staged the new movement and motor contracts
  beside the old ones. They replaced them instead: the locomotion markers,
  `MovementProfile`, `MovementState`, the ground/air pair, the marker-based
  registry and arbiter, `PlanarLocomotion`, and `CharacterController`'s
  in/out fields are gone rather than deprecated.
- **Systems iterate partitions, not registries.** Every movement system runs a
  cached query over the participating storage partitions.
  `World::ForEachComponent` walks every archetype, which on the unified World
  would simulate hidden and dormant partitions.
- **Dependency direction.** The movement layer includes nothing from physics;
  physics reaches movement in one translation unit, the mover pool's drive
  loop. Locomotion is therefore testable with no simulation at all.

Not yet done: the diagnostics surface. `ResolveMovementTuning` collects the
per-layer trace the plan asks for, but nothing displays it yet.

`ClingSession` and `FlightSession` ship as component definitions only. They are
the two models the mode registry's session mechanism is tested against
(`Register<Session>` and `RegisterWithCandidate<Session, Candidate>`); no
engine system drives either, because which of them a game wants and how it
enters them is exactly the part that should not be built in.

This plan replaces the earlier movement-only proposal. Review exposed two adjacent engine
contracts that should be repaired before movement profiles become real assets:

1. asset orchestration duplicates knowledge of every asset kind across `AssetSystem`,
   `AssetPreloader`, scanning, and hot reload;
2. Sencha has no generic substrate for structured, CPU-side data assets.

The ticket may therefore change the asset orchestration layer, add structured data assets,
ship a dedicated schema-driven Data Editor launched from Kettle, finish dependency-aware
async preloading, and then rebuild character movement on those foundations. It does not
replace the parts of the asset pipeline that are already correct: typed caches, typed runtime
handles, staged loading, owner-thread commit, asset identity, and domain-specific
decode/upload logic all remain.

The architectural goals are:

- physical facts are not locomotion modes;
- movement feel is authored as data over uniform algorithms;
- game modules can add or replace movement without engine edits;
- exactly one system composes the final motor request;
- assets are open to new structured data types without adding another loader/cache/switch
  family for each gameplay definition;
- adding a real outer asset kind requires one composition registration, not edits across
  every orchestration client;
- structured data is authorable through a first-class editor with descriptions, constraints,
  validation, undo/redo, and source-friendly files;
- game-defined data types appear in the prebuilt Data Editor without rebuilding it;
- all hot-path movement work uses compiled typed data, never JSON or string lookup.

---

## 1. Product fit and sign-off criteria

This architecture must support all of the following without genre-specific engine types:

- Loss Function: Quake-style free movement, strong air control, double jump, lunges,
  ascent/descent, tether forces, rocket impulses, knockback, and velocity-preserving
  spatial transitions.
- SINR and Zelda-like games: third-person locomotion, gliding, climbing or clinging,
  free flight, water response, moving platforms, root-motion actions, and contextual
  traversal.
- General action/adventure games: player or AI producers feeding the same movement
  intent, ability-driven actions, status effects, and per-character tuning.
- Platformers: predictable support facts, step negotiation, ground snapping, moving
  support inheritance, collision-corrected carried velocity, and room for coyote time
  and jump buffering.
- Game modules that replace Free locomotion, add a new locomotion mode, replace the
  default character motor, or omit the built-in movement recipe entirely.

The final gate is not "the template pawn moves." The gate is:

1. an FPS profile and a third-person profile use the same engine pipeline;
2. a game-module test adds a mode with no engine edit;
3. a game-module test replaces Free locomotion while keeping transitions, tuning, and
   the default motor;
4. a game-module test replaces the character motor while keeping ordinary Jolt world
   stepping;
5. async preload handles materials, skeletons, animation clips, skinned meshes, and
   structured data through one dependency-aware path;
6. Kettle launches a dedicated Data Editor for the active project and may deep-link to a
   specific `.sdata` asset;
7. a game-module test registers a new data subtype and its authoring schema, and the generic
   Data Editor can create, edit, validate, save, and reopen it with no editor rebuild;
8. movement-profile authoring exposes field descriptions, local condition controls,
   effective-value tracing, and live preview data;
9. movement diagnostics explain every resolved coefficient and motion contribution;
10. the full test suite stays green after every implementation slice.

---

## 2. Ground truth in the current tree

### 2.1 Movement

The current movement stack already contains useful pieces, but its state model is wrong.

- Ground and air are exclusive marker components, `OnGround` and `InAir`, swapped by
  `LocomotionModeRequest` and a priority arbiter.
- The arbiter also projects `movement.grounded` and `movement.airborne` tags, so one
  physical fact is represented three times: marker membership, request/arbiter state,
  and gameplay tags.
- `GroundLocomotionSystem` and `AirLocomotionSystem` are the same planar algorithm with
  different coefficients. Their split is policy encoded as exclusive archetypes.
- `MovementProfile` is currently an ECS component with hardcoded defaults, overwritten
  from global cvars.
- `MovementIntent.WishDir` is world-space and planar.
- Jump is already AbilityKit data. Activation grants a short request tag and
  `JumpExecutionSystem` consumes it.
- `MoveSpeed` is an attribute, intentionally modified through effects.

### 2.2 Character motor and physics

- `CharacterController` currently mixes authored capsule configuration, motion request,
  and grounded readback.
- `CharacterMover` wraps Jolt `CharacterVirtual` behind the existing PIMPL firewall.
- The mover owns vertical velocity and gravity integration, accepts planar velocity,
  hardcodes world-up, and re-gates jump against its own grounded state.
- Jolt is the landed backend. This plan reshapes the motor API but does not add another
  backend abstraction.

### 2.3 Schedule and ECS

- FixedLogic runs before Physics in each fixed tick.
- Physics facts consumed by FixedLogic are therefore one tick old.
- Systems are plain scheduled types ordered by `After<T, TDep>`.
- Components are trivially copyable.
- Structural changes during queries use `CommandBuffer` or collect-then-apply.
- Component masks are fixed at 256 types.
- Queries are cached and `Without<T>` is archetype-level.
- Expected character count is tens, not thousands.

### 2.4 AbilityKit and tags

- AbilityKit is callback-free. Reactions are systems polling data.
- `AbilityDefinition.OnActivate` is an effect id, not a C++ handler.
- `GameplayTagContainer` has a fixed active-entry cap of 32 and counted grants.
- Ability gating reads gameplay tags, not arbitrary component absence.
- Channeled and toggle ability tasks are deferred, so initial hold/toggle movement verbs
  need a producer-managed bridge.

### 2.5 Asset pipeline

The asset pipeline has a sound lower half:

- `AssetRegistry`, `AssetRef`, `AssetId`, paths, and content hashes provide identity.
- `IAssetSource` isolates byte acquisition.
- `IAssetLoader::LoadStaged` performs pure task-thread work.
- commit happens on the owner thread.
- concrete loaders decode genuinely different formats;
- concrete caches own genuinely different resources and typed handles;
- hot reload replaces resident entries in place.

The orchestration layer is the problem:

- `AssetType` conversion is a closed switch;
- extension recognition is hardcoded;
- `AssetSystem` owns or points at every concrete loader and cache;
- `Load*`, `TryAcquire*`, `Release*`, `IsResident`, and `LoaderFor` repeat type knowledge;
- `AssetPreload` has one vector per handle type;
- `AssetPreloader` switches by type to acquire, classify waves, commit, deliver, and
  release;
- `AssetHotReloader` switches by type again;
- `IAssetLoader::Commit` returns only success/failure, so generic orchestration cannot
  retain the creation reference and falls back to concrete `CommitTyped` calls.

Adding a movement profile as a bespoke `.sprof` asset would copy this cost and encourage
the same mistake for input maps, animation graphs, camera profiles, AI tables, item
definitions, quests, dialogue, HUD layouts, and weapon data.

### 2.6 Editor family and Kettle

Sencha already has the correct product topology for data authoring:

- `editor/common` provides the shared ImGui shell, commands, input, project mounting, and
  process launch;
- Kyusu and Shudei are separate applications over that shell;
- Kettle is the project hub and launches tools with the selected `--project`;
- editors load the project's content roots, asset registry, theme, console, and logging;
- project game modules are intended to register authoring metadata into editor-owned
  registries without running gameplay during ordinary authoring.

Structured data therefore should not become a large panel inside Kettle or Kyusu. It should
be another focused editor application launched by Kettle and reachable through asset deep
links from the other tools.

---

## 3. Decisions

### D1. Preserve concrete loaders and caches

Do not build one universal cache or one universal decoder.

Static meshes, textures, materials, audio, skeletons, clips, skinned meshes, and
structured data have different payloads, dependencies, upload work, teardown, and typed
runtime consumers. Their concrete loaders and caches remain.

The refactor is above them: registration, generic residency, preload orchestration, and
hot reload.

### D2. Keep `AssetType` as the stable outer category for now

`AssetType` remains a stable serialized enum. Add exactly one new outer category:

```cpp
AssetType::Data
```

Game modules do not initially register new outer transport/lifetime categories. Their
open extension point is a subtype of `Data`.

Revisit a dynamic outer `AssetKindId` only when a second real module-defined asset kind
appears that cannot fit structured data. A hypothetical custom GPU asset is not enough
reason to destabilize serialized asset categories now.

### D3. Register operations for each outer asset kind

Add an `AssetKindRegistry` or equivalently named operations table. Each entry contains
the operations generic orchestration actually needs:

```cpp
struct AssetKindRegistration
{
    AssetType Type;
    std::string_view Name;
    std::vector<std::string> RuntimeExtensions;

    IAssetLoader* Loader;
    IAssetStore* Store;

    AssetPreloadPolicy Preload;
};
```

`RuntimeAssets` remains the concrete composition root and registers the built-in kinds
after constructing their loaders and stores.

Generic clients ask the registration table instead of switching on `AssetType`.

### D4. Add a type-erased asset lease

Add a move-only `AssetLease` representing one retained runtime reference:

```cpp
struct AssetLease
{
    AssetType Type;
    ILifetimeOwner* Owner;
    uint64_t Token;
};
```

The exact representation may reuse `Owned<uint64_t>` or wrap it. Requirements:

- move-only;
- destruction releases exactly once;
- an invalid lease owns nothing;
- the token stays opaque outside its store;
- a typed facade can recover the typed handle only after validating the outer type.

Add an `IAssetStore` narrow enough for generic orchestration:

```cpp
class IAssetStore : public ILifetimeOwner
{
public:
    virtual ~IAssetStore() = default;

    virtual bool IsResident(std::string_view path) const = 0;
    virtual AssetLease TryAcquire(std::string_view path) = 0;
    virtual std::string_view GetPath(uint64_t token) const = 0;
};
```

Existing caches adapt to this interface while keeping all typed APIs used by render,
animation, audio, and gameplay code.

### D5. Successful commit returns the creation lease

Replace the Boolean-only virtual commit result with ownership:

```cpp
struct AssetCommitResult
{
    AssetLease Created;
    std::string Error;

    bool IsValid() const;
};
```

The successful commit contract is:

> The loader inserts or replaces the runtime entry and returns the retained creation
> reference as an `AssetLease`.

This lets generic orchestration:

1. commit without naming the concrete handle;
2. acquire one lease for each waiter;
3. let the creation lease release naturally after delivery.

Concrete `CommitTyped` helpers may remain as typed facades, but the preloader and hot
reload driver must not call them.

### D6. Dependencies are reported by staging, not hardcoded as waves

Extend `AssetStaging`:

```cpp
struct AssetStaging
{
    AssetRecord Record;
    std::any Payload;
    std::vector<AssetRef> Dependencies;
    std::string Error;
};
```

The staged payload has already parsed enough data to know its dependencies. Examples:

- material -> textures;
- skinned mesh -> skeleton;
- animation clip -> skeleton;
- animation graph -> clips;
- dialogue data -> audio;
- structured game definition -> referenced data or presentation assets.

`AssetPreloader` becomes a dependency-aware residency scheduler:

- stage each requested asset once;
- coalesce duplicate in-flight requests;
- register dependencies returned by staging;
- stage missing dependencies;
- commit an asset only after all dependencies are resident;
- detect cycles and report the path;
- preserve the existing owner-thread commit budget;
- keep dependency ownership in the concrete cache entry, not in the preloader;
- release preload scaffold leases after the real consumer takes ownership.

This deletes the material-specific second wave and finishes async preload for skeletons,
animation clips, and skinned meshes.

This is not a general build-system DAG. It is a small runtime residency graph over
`AssetRef` with one purpose: dependency-safe commit.

### D7. Hot reload uses the same registered operations

`IAssetLoader` gains an optional reload operation or the kind registration carries one:

```cpp
virtual bool CanReload() const;
virtual AssetReloadResult CommitReload(AssetStaging&& staged);
```

`AssetHotReloader` resolves the kind registration, stages through its loader, ensures
new dependencies are resident, and invokes reload without a type switch.

Reload preserves existing typed handles. If dependencies change, the concrete cache
entry acquires the new dependencies before releasing the old ones.

### D8. Extension recognition is registered

Runtime extensions belong to the outer asset-kind registration.

Directory scanning asks the registry:

```text
.smesh  -> StaticMesh
.skmesh -> SkinnedMesh
.smat   -> Material
.sdata  -> Data
```

Importer source extensions remain in `AssetImporterRegistry`; runtime-format recognition
and source importing stay separate.

### D9. Structured data assets are one outer asset kind with open subtypes

Add:

```text
DataAssetLoader
DataAssetCache
DataAssetHandle
DataAssetTypeRegistry
```

A `.sdata` file has a stable envelope:

```json
{
  "type": "movement.profile",
  "version": 1,
  "data": {
  }
}
```

A registered data type provides:

```cpp
struct DataAssetTypeRegistration
{
    std::string Name;
    uint32_t CurrentVersion;

    CompileDataAssetFn Compile;
    DestroyDataAssetFn Destroy;
};
```

The compile callback:

- receives parsed JSON;
- validates required fields and constraints;
- produces an immutable typed runtime value;
- reports asset dependencies;
- never touches a World, renderer, or other mutable engine state;
- is safe to run in `LoadStaged`.

The data cache stores a type id, a generational handle, the immutable compiled value,
its destruction operation, and a reload version.

Typed consumers use narrow wrappers over `DataAssetHandle`, for example:

```cpp
struct MovementProfileHandle
{
    DataAssetHandle Value;
};
```

The wrapper adds semantic type safety. It does not create another cache or loader.

### D10. Game modules register runtime data types and neutral authoring schemas

Add explicit module hooks, names to match the existing `Game` lifecycle:

```cpp
OnRegisterDataAssetTypes(DataAssetTypeRegistry&, DataSchemaRegistry&)
OnUnregisterDataAssetTypes(DataAssetTypeRegistry&, DataSchemaRegistry&)
```

Engine data types register through the same registries. `DataSchema` is engine-owned neutral
metadata. It contains no ImGui types and is safe for runtime hosts to ignore after
registration. The prebuilt Data Editor loads the project game module for this registration
surface, not to tick gameplay.

The module-unload contract is:

- schemas and subtype registrations are removed before module code unloads;
- no compiled value whose destroy function lives in the module may survive
  unregistration;
- no editor document may retain schema pointers after unregistration;
- current editor/runtime shutdown ordering must release data asset cache entries and editor
  documents first;
- future game-module hot reload must explicitly drain or migrate resident game-defined data
  and close or rebind open editor documents before unloading the old module.

The ABI fingerprint changes with the new hooks.

### D11. Structured data has a dedicated authoring schema

Do not stretch `RuntimeSchema` into this role. `RuntimeSchema` is a flattened scalar view of
trivially copyable component memory. Structured assets need records, arrays, optional
members, references, constraints, documentation, and stable source keys.

Add a separate, declarative `DataSchema` contract. Names are sketches:

```cpp
enum class DataFieldKind
{
    Bool,
    Int,
    Float,
    String,
    Enum,
    Vector,
    Record,
    Array,
    Optional,
    AssetRef,
    DataAssetRef,
    GameplayTag
};

struct DataFieldSchema
{
    std::string_view Key;
    std::string_view DisplayName;
    std::string_view Summary;
    std::string_view Description;
    std::string_view Units;

    DataFieldKind Kind;
    DataDefaultValue Default;
    DataConstraints Constraints;
    DataEditorHint EditorHint;

    std::span<const DataFieldSchema> Children;
    bool Advanced = false;
    bool ReadOnly = false;
};

struct DataSchema
{
    std::string_view TypeName;
    std::string_view DisplayName;
    std::string_view Description;
    DataFieldSchema Root;
};
```

The exact value representation may change during implementation, but the contract must
support the first real consumers:

- nested records;
- ordered arrays with insert, delete, duplicate, and reorder;
- optional sections;
- enum choices with readable labels;
- numeric min/max/step and unit metadata;
- ordinary asset references filtered by `AssetType`;
- data-asset references filtered by subtype;
- gameplay-tag selection;
- defaults, advanced fields, deprecation, and read-only fields;
- short hover summaries and longer persistent documentation;
- JSON-path-aware structural errors.

Validation has two layers:

1. the generic schema validates structure, field kinds, required values, references, and
   simple constraints;
2. the subtype compiler performs semantic validation and produces the immutable runtime
   value.

The form and the compiler share source keys and JSON paths so every error selects the exact
field that caused it. The schema remains authoring metadata, not runtime reflection and not
an object serializer.

### D12. Structured data gets a dedicated editor application launched from Kettle

Add a new editor application over `editor_common`. The product name can be chosen later;
the internal target may remain mechanically named `data_editor` until then.

Kettle remains the hub and gains:

- **Open Data Editor** for the selected project;
- recent data assets;
- create-new shortcuts grouped by registered subtype;
- deep-link launch with `--project <path> --asset <asset://...>`.

Kyusu, Shudei, inspectors, and asset browsers may invoke the same deep link through an
**Open in Data Editor** action. Kettle does not embed the editor workspace.

The Data Editor workspace is document-oriented:

```text
Asset/type browser | Generated form or subtype workspace | Documentation/preview
Validation, references, external-change status, and save state across the bottom
```

Required UX:

- search and filter by path, folder, and registered subtype;
- create, open, duplicate, rename, and delete `.sdata` assets;
- multi-document tabs with dirty indicators;
- undo/redo through `editor_common::CommandStack`;
- generated controls for every v1 `DataFieldKind`;
- array reordering and duplication without editing indices manually;
- searchable asset, data-subtype, enum, and gameplay-tag pickers;
- a short tooltip from `Summary` and a persistent documentation pane from `Description`;
- visible default, units, range, source key, and validation state for the selected field;
- continuous structural and semantic validation;
- clicking an error selects and scrolls to the field;
- raw JSON view for inspection and expert editing, with round-trip preservation of the
  canonical data model;
- explicit external-change conflict handling when the document is dirty;
- source-control-friendly deterministic formatting on save;
- save-triggered hot reload, never runtime mutation on every keystroke.

The structured form always emits parseable JSON. Semantic validation errors may be saved so
in-progress work is not trapped in the editor, but cook/load/hot reload rejects the invalid
revision and keeps the last known-good resident value. The UI must state that clearly.

### D13. The generic editor is complete; specialized workspaces are earned per subtype

Every registered subtype must be authorable through `DataSchema` with no custom editor
code. That is the game-module extensibility guarantee.

The Data Editor may also provide built-in subtype workspaces where visualization adds real
value. Do not design a general game-module editor-plugin ABI in this ticket. The first
specialized workspace is `movement.profile`, implemented inside the Data Editor because it
is an engine subtype and a concrete consumer.

Its best version includes:

- an ordered layer list with add, duplicate, delete, and drag reorder;
- a focused condition section using movement-specific controls for support, immersion,
  mode, and tag query fields;
- a context simulator for support kind, immersion, active mode, tags, and MoveSpeed;
- matched and unmatched layer trace with the failing condition identified;
- effective-value table showing base, each operation, and final coefficient;
- acceleration response and jump-arc preview using the same pure movement math kernels;
- comparison against another movement profile;
- clear indication of which values come from attributes rather than profile data;
- field descriptions and units visible without hovering.

This is not a second file format or compiler. It edits the same schema-backed document and
uses the same subtype validator. If specialized workspaces later repeat a real extension
shape, extract an editor extension registry then.

### D14. Data assets store stable names, not World-local runtime ids

Gameplay tag ids and locomotion mode ids are registration-order runtime values. A global
data asset cannot bake ids from one World and assume another World uses the same values.

A movement profile therefore compiles in two stages:

1. `DataAssetLoader` parses and validates the portable profile into names and typed
   numeric operations.
2. A per-World `MovementProfileBindingCache` binds tag names and mode names to that
   World's registries once, producing a `BoundMovementProfile`.

The binding cache is keyed by:

- data asset handle;
- data asset reload version;
- relevant registry generation/version.

Fixed-tick movement reads only `BoundMovementProfile`. No strings or JSON are evaluated
in the simulation loop.

Unknown tag or mode names are binding errors. The affected character uses an explicit
fallback profile and emits one diagnostic, not a warning every tick.

### D15. Ground and air are support facts, not modes

Replace `OnGround` and `InAir` with a resident `SupportState` field:

```cpp
enum class SupportKind : uint8_t
{
    None,
    Stable,
    Steep
};

struct SupportState
{
    EntityId Surface;
    Vec3d ContactPoint;
    Vec3d Normal;
    Vec3d SurfaceVelocity;
    SupportKind Kind;
};
```

Resident fields avoid archetype migration on every jump and landing. Support velocity
is required for moving and rotating platforms.

The motor is the sole writer.

### D16. Modes are reserved for control-frame or lifecycle changes

A locomotion mode is justified when movement gains at least one of:

- different controllable degrees of freedom;
- a different movement frame or up axis;
- transient state with entry/exit lifecycle;
- exclusive ownership of motion production.

Therefore:

- Free is the ordinary support/air algorithm.
- Glide is tuning while it only changes gravity and air response.
- Flight is a mode because it enables full 3D intent.
- Cling is a mode plus session because it changes the movement plane and owns a surface.
- Water is tuning until a game needs swim-specific control interpretation.
- ladders, vehicles, authored traversal, and true swimming can become game-defined modes
  when their consumers arrive.

### D17. Mode identity is open without strategy objects

`LocomotionModeRegistry` interns dotted names into runtime ids and records:

- active gameplay tag;
- ability request tag;
- session component operations;
- optional profile block name.

Adding a game mode requires:

- a registered name;
- a session component if the mode owns transient state;
- one scheduled locomotion system;
- ability or game logic that requests entry;
- profile data.

There is no closed engine enum, virtual locomotion strategy hierarchy, or central
`switch(mode)`.

### D18. Mode, session, and active tag change atomically

`LocomotionModeTransitionSystem` is the sole owner of:

- `CharacterMovement.Mode`;
- mode session add/remove;
- active `movement.mode.<name>` tag;
- pending transition consumption.

The operation removes the old session and tag, sets the new mode, adds the new session,
and grants the new tag in one collect-then-apply transition.

The recurring tag projection system projects support only. It does not rewrite mode tags
one tick after the authoritative transition.

### D19. Transition requests use semantic classes, not mode priorities

Use:

```cpp
enum class ModeRequestClass : uint8_t
{
    Automatic,
    Explicit,
    Forced
};
```

Examples:

- Automatic: flight sustain condition failed, cling surface lost.
- Explicit: player or AI activated a mode-entry or toggle-exit verb.
- Forced: death, stun, cinematic possession, hard gameplay interrupt.

The mailbox rule is:

- higher request class replaces lower request class;
- within the same class, first write wins;
- ability data still expresses semantic exclusions between modes;
- no global ranking exists between individual modes.

This prevents a stale automatic return-to-Free request from eating fresh player input
while preserving deterministic conflict resolution.

### D20. Intent is world-space 3D

```cpp
struct MovementIntent
{
    Vec3d WishDir;
};
```

The producer resolves camera, actor, rail, or AI framing. Magnitude carries input
strength. Producers without a vertical axis leave its vertical component zero.

Each locomotion algorithm projects the intent into its degrees of freedom.

### D21. Tuning has one owner per coefficient

`MoveSpeed` remains an attribute. Sprint, haste, slow, and ordinary stat effects modify
it through AbilityKit.

Movement profile layers own mechanical response to facts and state:

- acceleration;
- friction;
- stop speed;
- wish-speed cap;
- drag;
- gravity scale;
- jump speed;
- environmental scaling;
- mode-specific coefficients.

Resolution order is:

1. attribute-derived values;
2. profile base layer;
3. matching shared layers in authored order;
4. matching active-mode layers in authored order.

Within a layer, operations apply `set`, then `scale`, then `add`.

There is no specificity calculation and no numeric rule priority.

### D22. Motion contributors do not post-write the final request

The earlier plan let locomotion, jump, dash, and game action systems mutate
`MotionRequest` in schedule order. That makes gameplay correctness depend on which
concrete system ran last.

Replace it with explicit contributions:

```cpp
struct LocomotionOutput
{
    Vec3d Velocity;
    Vec3d UpAxis;
    float GravityScale;
};

struct MotionAxisOverride
{
    bool HasPlanar;
    Vec3d PlanarVelocity;

    bool HasUp;
    float UpVelocity;
};

struct MotionImpulse
{
    Vec3d DeltaVelocity;
};

struct MotionRequest
{
    Vec3d Velocity;
    Vec3d UpAxis;
    float GravityScale;
};
```

`MotionCompositionSystem` is the sole writer of `MotionRequest`.

Initial composition order:

1. locomotion base velocity relative to support;
2. support surface velocity while stably supported;
3. optional root-motion contribution when animation runtime supplies it;
4. planar and up-axis action overrides;
5. additive impulses and constraint corrections;
6. final collision request.

Rules:

- additive impulse accumulation is order-independent;
- planar and up override channels may compose, so jump and dash can occur together;
- overlapping writes to the same override channel are first-write-wins and must be
  prevented semantically through ability gating where possible;
- forced gameplay reactions may explicitly replace an existing override through a
  separate API;
- contributors never write `MotionRequest` directly.

The ticket implements locomotion output, axis overrides, impulses, and composition.
Root motion receives an explicit insertion point but its producer lands with animation.

### D23. The motor is collide-and-slide plus fact production

`CharacterController` becomes authored physical configuration:

```cpp
struct CharacterController
{
    float Radius;
    float Height;
    float SlopeLimitDegrees;
    float StepHeight;
    float GroundSnapDistance;
    float SkinWidth;
};
```

`CharacterMover` accepts:

```cpp
Move(velocity, dt, gravity, upAxis)
```

and reports:

- achieved world velocity;
- support kind;
- support normal;
- contact point;
- support entity;
- support velocity at the contact point.

Locomotion owns velocity integration. Jolt still receives gravity for its support and
slide behavior, but does not secretly maintain a second vertical state.

Free locomotion operates in the support-relative frame while stably supported, then
composition adds support velocity. This avoids both sliding off moving platforms and
double-counting their motion.

The Jolt PIMPL remains the backend firewall.

### D24. Grounded projection exists because AbilityKit needs it

`movement.grounded` is derived from `SupportState.Kind == Stable` and projected before
`AbilityActivationSystem`.

There is no `movement.airborne` tag. `None(movement.grounded)` expresses airborne gates.

Mode tags are maintained atomically by transitions. Volume-membership tags are maintained
by volume sensing. No other movement projections ship without a named consumer.

### D25. Volume sensing is opt-in and registry-owned

Characters participate by carrying `Immersion` or a dedicated `VolumeSensor` component.

`VolumeSensingSystem`:

- uses a dedicated query/collision layer;
- performs capsule overlap only for participating characters;
- writes immersion fraction;
- reconciles counted volume tags;
- stores previous overlap bookkeeping in a per-World resource keyed by generational
  entity ids;
- clears naturally when the World detaches;
- exposes overlap counts to profiling.

Opaque process-lifetime system-local maps are rejected because streamed registries and
entity recycling make their lifecycle ambiguous.

### D26. Registration is granular

Keep a convenience recipe, but split registration so a game can replace one layer:

```cpp
RegisterMovementComponents(world);
RegisterDefaultMovementAbilities(world);

RegisterMovementFactSystems(schedule);
RegisterMovementTransitionSystems(schedule);
RegisterMovementTuningSystems(schedule);
RegisterFreeLocomotionSystem(schedule);
RegisterDefaultMotionComposition(schedule);
RegisterDefaultCharacterMotorSystems(schedule);

RegisterDefaultMovementSystems(schedule);
```

Likewise split physics registration so ordinary Jolt stepping is separable from the
default character motor.

A game can:

- use the full default recipe;
- replace Free locomotion only;
- replace composition only;
- replace the character motor only;
- add modes;
- omit all built-in movement.

No runtime system unregister mechanism is required. Selection occurs at game-module
composition before `EngineSchedule::Init()`.

### D27. Movement diagnostics are part of the feature

Data-driven behavior must be explainable.

Add a debug inspection path that shows, for one entity:

- support, immersion, and achieved velocity facts;
- current mode, pending request class, and session;
- active movement tags;
- movement profile asset and bound version;
- each matched profile layer in application order;
- final resolved coefficients;
- locomotion output;
- support velocity;
- action overrides;
- accumulated impulses;
- final `MotionRequest`;
- achieved post-collision velocity.

Also expose active tag count and warn before the fixed capacity is exhausted.

---

## 4. Structured movement profile

Movement profile is the first serious `AssetType::Data` consumer.

### 4.1 File

Example `assets/gameplay/ada_movement.sdata`:

```json
{
  "type": "movement.profile",
  "version": 1,
  "data": {
    "name": "ada",
    "layers": [
      {
        "set": {
          "acceleration": 24.0,
          "friction": 8.0,
          "stop_speed": 1.0,
          "gravity_scale": 1.0,
          "drag": 0.0,
          "wish_speed_cap": 0.0,
          "jump_speed": 5.5
        }
      },
      {
        "when": {
          "support": "none"
        },
        "set": {
          "friction": 0.0,
          "acceleration": 10.0,
          "wish_speed_cap": 1.0
        }
      },
      {
        "when": {
          "immersion_at_least": 0.6
        },
        "set": {
          "gravity_scale": 0.2,
          "drag": 4.0
        },
        "scale": {
          "max_speed": 0.6
        }
      },
      {
        "when": {
          "tags": {
            "all": ["movement.gliding"]
          }
        },
        "set": {
          "gravity_scale": 0.25,
          "acceleration": 12.0,
          "wish_speed_cap": 6.0
        }
      }
    ],
    "modes": {
      "movement.mode.flight": {
        "sustain": {
          "all": ["volume.mist"]
        },
        "layers": [
          {
            "set": {
              "acceleration": 30.0,
              "drag": 2.0,
              "gravity_scale": 0.0
            }
          }
        ]
      }
    }
  }
}
```

### 4.2 Portable compiled form

The data-type compiler produces immutable typed records containing:

- numeric layer operations;
- support and immersion predicates;
- stable tag names;
- stable mode names;
- mode sustain queries;
- source JSON paths for diagnostics;
- no World-local ids.

### 4.3 Bound form

`MovementProfileBindingCache` resolves names against one World and produces:

```text
BoundMovementProfile
  ordered shared layers
  ordered mode layers
  interned gameplay tag queries
  interned locomotion mode ids
  source layer indices for diagnostics
```

Hot reload increments the data entry version. The next resolver access rebuilds the
bound profile once.

### 4.4 Resolution semantics

- `max_speed` starts from the current `MoveSpeed` attribute.
- A profile may `set max_speed` only for entities without an `AttributeSet`.
- `when` is a movement-profile-local condition grammar, not a generic engine rules
  substrate.
- Conditions in one layer are ANDed.
- Initial condition vocabulary: mode, support, immersion threshold, and gameplay tag query.
- The data editor exposes dedicated controls for those fields rather than a generic rule
  graph or predicate registry.
- Nested arbitrary boolean expressions are not part of v1. Extend the movement grammar only
  when a concrete movement profile cannot be expressed by ordered layers and flat AND
  conditions.
- Mode blocks append after shared layers.
- Unknown fields are validation errors.
- Unknown mode or tag names are binding errors.
- Duplicate keys and invalid numeric ranges fail the asset.
- Runtime resolution performs no allocation, JSON traversal, hashing, or string lookup.

### 4.5 Movement profile authoring surface

The movement workspace edits the same `.sdata` document as the generic form.

The context bar supplies test facts without running a World:

```text
Support: Stable | Immersion: 0.00 | Mode: Free | MoveSpeed: 7.0 | Tags: [...] 
```

For that context the editor shows:

- every layer in authored order;
- matched or unmatched state;
- the first failed local condition;
- each set, scale, and add operation;
- final values and their provenance;
- jump arc and acceleration response previews from the pure runtime kernels.

Changing preview context never changes the asset. Editing the asset recomputes the preview
from the working document. Save writes deterministic `.sdata`; a valid save triggers normal
hot reload.

---

## 5. Movement components and resources

All ECS components are trivially copyable.

| Component | Category | Purpose |
|---|---|---|
| `SupportState` | fact | Stable/steep/none support, contact, surface velocity |
| `Immersion` | fact | Current sensed volume and fraction |
| `KinematicState` | fact | Achieved post-collision world velocity |
| `CharacterMovement` | control | Typed profile handle and current mode |
| `ModeTransitionRequest` | control request | Requested mode, semantic request class, source |
| `MovementIntent` | control input | World-space 3D desired direction and strength |
| `ResolvedMovementTuning` | tuning | Final numeric coefficients |
| `LocomotionOutput` | motion contribution | Exclusive base producer output |
| `MotionAxisOverride` | motion contribution | Optional planar/up action override mailboxes |
| `MotionImpulse` | motion contribution | Additive delta velocity accumulator |
| `MotionRequest` | motor request | Sole composed motor input |
| `ClingSession` | session | Surface entity, normal, anchor/contact state |
| `FlightSession` | session | Zero-size in v1, retained for exclusive filtering |

World resources:

- `LocomotionModeRegistry`;
- `MovementTags`;
- `MovementDefs`;
- `MovementProfileBindingCache`;
- `VolumeOverlapState`;
- optional movement diagnostics state.

Deleted or reshaped:

- delete `OnGround`;
- delete `InAir`;
- delete the old `LocomotionModeRequest` and priority arbiter;
- delete `GroundingTransitionSystem`;
- delete `MovementState`, replaced by `KinematicState`;
- delete `MovementProfile` as an ECS tuning blob;
- delete global movement tuning cvar overwrite;
- trim `MovementTags.Airborne`;
- reshape `CharacterController` to physical configuration only.

---

## 6. Fixed-tick systems and ordering

All movement work is serial unless profiling later crosses the existing roughly 1 ms
parallelization gate.

### 6.1 FixedLogic

1. **Game input or AI producer**
   - writes `MovementIntent`;
   - queues ability activations;
   - manages temporary hold tags until AbilityKit toggle/channel tasks exist.

2. **`VolumeSensingSystem`**
   - writes `Immersion`;
   - reconciles counted volume tags.

3. **`SupportTagProjectionSystem`**
   - reads prior Physics `SupportState`;
   - grants or revokes `movement.grounded`.

4. **`AbilityActivationSystem`**
   - activates jump and mode-entry abilities;
   - effects grant request tags, cooldowns, costs, and action tags.

5. **`ModeRequestCollectionSystem`**
   - converts mode request tags into Explicit transition requests;
   - preserves requests already written by Forced gameplay systems;
   - revokes consumed request tags.

6. **`LocomotionModeTransitionSystem`**
   - resolves request classes;
   - atomically swaps mode field, session, and active mode tag;
   - clears the request.

7. **`AttributeResolveSystem`**
   - resolves `MoveSpeed` and other attributes after effects.

8. **`MovementTuningResolutionSystem`**
   - binds or retrieves the profile;
   - evaluates ordered layers into `ResolvedMovementTuning`;
   - records layer trace when diagnostics are enabled.

9. **Exactly one locomotion producer per entity**
   - `FreeLocomotionSystem` for Free;
   - `ClingLocomotionSystem` for `ClingSession`;
   - `FlightLocomotionSystem` for `FlightSession`;
   - game-module systems for game-defined sessions.
   - each writes `LocomotionOutput`, never `MotionRequest`.

10. **`JumpExecutionSystem`**
    - consumes the jump request tag;
    - writes only the up-axis action override;
    - does not re-check Jolt grounding.

11. **Game action and force systems**
    - dash/lunge writes planar override;
    - ground pound writes up override;
    - explosions and knockback add `MotionImpulse`;
    - tether adds a correction impulse or action override;
    - forced reactions may replace an occupied override through the explicit forced API.

12. **`MotionCompositionSystem`**
    - sole writer of `MotionRequest`;
    - consumes/clears transient overrides and impulses after composition.

13. **`EffectLifetimeSystem`**
    - ages and removes effects.

Automatic mode exits detected by a locomotion system write an Automatic request for the
next fixed tick. A same-tick Explicit or Forced request can replace it according to the
semantic class rule.

### 6.2 Physics

14. **`PhysicsStepSystem`**
    - advances ordinary Jolt world state.

15. **`CharacterControllerSystem` and `CharacterMoverPool`**
    - consume `MotionRequest`;
    - perform collide-and-slide;
    - write `LocalTransform`, `KinematicState`, and `SupportState`.

### 6.3 Staleness contract

FixedLogic consumes support, achieved velocity, and transform from the previous Physics
step. Volume sensing uses the current FixedLogic view of that previous position.

This is explicit and testable. No mid-tick barrier is added.

### 6.4 Determinism

- fixed-tick systems are topologically ordered;
- same-class mailboxes use first-write-wins;
- request-class replacement is deterministic;
- additive impulses sum in deterministic system order, with tests against the reference
  single-thread path;
- registries use stable vector registration order where iteration order matters;
- no unordered-container iteration determines gameplay results;
- structural mode transitions are collect-then-apply.

---

## 7. Locomotion algorithms

### 7.1 Free

Reads intent, tuning, support, and achieved velocity.

- derive support-relative velocity when stably supported;
- apply planar friction only when the resolved profile requests it;
- accelerate toward planar wish;
- enforce wish-speed cap independently of maximum velocity;
- apply drag;
- integrate gravity along the up axis;
- prevent downward support-relative velocity while stably supported;
- write world-frame `LocomotionOutput` before support velocity is added by composition.

Ground and air differ only through resolved coefficients.

### 7.2 Glide

Glide remains Free locomotion with a profile layer keyed by a hold tag.

Escalation criterion:

- if glide later adds pitch-steered climb/dive, a different controllable axis, or
  transient lifecycle state, it becomes a Flight configuration or game-defined mode.

### 7.3 Cling

`ClingLocomotionSystem` queries `ClingSession`.

- entry captures a reachable surface through a shapecast/raycast;
- wish is projected into the surface plane;
- up axis becomes opposite the support normal;
- gravity scale resolves to zero unless profile data says otherwise;
- session refreshes contact/anchor state;
- lost or invalid surface writes an Automatic transition to Free.

An activation with no reachable surface may enter and automatically exit in the first
implementation. Add a candidate projection only if playtesting proves press-feel requires
pre-gating.

### 7.4 Flight

`FlightLocomotionSystem` queries `FlightSession`.

- consumes full 3D wish;
- accelerates toward wish;
- applies drag;
- usually resolves gravity scale to zero;
- evaluates the profile's sustain query;
- failed sustain writes an Automatic transition to Free.

SINR's mist fiction exists in ability names, tags, volumes, presentation, and profile
data. The engine mechanism is sustained flight.

---

## 8. End-to-end traces

### Grounded walk

1. prior Physics wrote stable support and surface velocity;
2. grounded tag is projected;
3. profile resolves ground coefficients;
4. Free computes support-relative planar locomotion;
5. composition adds support velocity;
6. motor collides and writes achieved facts.

### Walk off a ledge

1. motor reports `SupportKind::None`;
2. grounded tag is revoked;
3. airborne profile layer changes friction, acceleration, gravity, and wish cap;
4. the same Free algorithm continues;
5. no mode transition or archetype migration occurs.

### Jump plus dash

1. input queues jump and dash abilities;
2. abilities pass their tag/cooldown gates;
3. jump writes the up-axis override;
4. dash writes the planar override;
5. composition combines both with locomotion and impulses;
6. one final request reaches the motor.

### Rocket impulse

1. hit query identifies the character;
2. impulse execution adds a delta velocity to `MotionImpulse`;
3. composition adds it after ordinary action overrides;
4. the motor produces collision-corrected achieved velocity;
5. next tick locomotion carries the achieved result.

This is the landing point for the roadmap's `IImpulseSink`.

### Moving platform

1. motor reports stable support and surface velocity at the contact point;
2. Free controls velocity relative to that support;
3. composition adds the support velocity;
4. stepping, snap, and collision keep the character attached without baking platform
   logic into locomotion branches.

### Cling entry versus automatic flight exit

1. flight had queued an Automatic return to Free after losing sustain;
2. the player activates cling before transition application;
3. Explicit cling replaces Automatic Free;
4. transition atomically swaps Flight session/tag to Cling session/tag;
5. cling produces motion in the surface frame in the same tick.

### Structured data hot reload

1. `ada_movement.sdata` changes;
2. generic hot reload stages through `DataAssetLoader`;
3. `movement.profile` recompiles to an immutable portable profile;
4. `DataAssetCache` swaps the resident value in place and increments version;
5. the World's binding cache notices the version mismatch and rebuilds once;
6. existing character handles remain valid.

---

## 9. Asset API result after refactor

Typed domain code remains direct:

```cpp
StaticMeshHandle mesh = assets.LoadStaticMesh(path);
MaterialHandle material = assets.LoadMaterial(path);
const MovementProfile* profile = dataAssets.TryGet<MovementProfile>(handle);
```

Generic orchestration becomes type-agnostic:

```cpp
const AssetKindRegistration* kind = kinds.Find(record.Type);
AssetLease resident = kind->Store->TryAcquire(record.Path);
AssetStaging staged = kind->Loader->LoadStaged(record, source);
AssetCommitResult committed = kind->Loader->Commit(std::move(staged));
```

`RuntimeAssets` still visibly owns:

```text
TextureCache
MaterialCache
MaterialSetCache
SkeletonCache
StaticMeshCache
SkinnedMeshCache
AnimationClipCache
AudioClipCache
DataAssetCache
AssetSystem
```

This preserves clear domain ownership and load-bearing destruction order.

The refactor deletes orchestration-only concrete knowledge:

- concrete loader getters from `AssetSystem`;
- `LoaderFor` switch;
- `IsResident` switch;
- hardcoded runtime extension chain;
- typed preload handle vectors;
- preloader acquire/commit/deliver switches;
- material-specific wave storage;
- hot-reload type switch.

---

## 10. Extensibility contracts

### Add a structured game data type

A game module:

1. defines a compiled immutable C++ type;
2. registers `game.weapon`, `sinr.item`, or another stable name;
3. provides compile and destroy operations;
4. registers a neutral `DataSchema` with descriptions, constraints, and defaults;
5. references it through `.sdata`;
6. optionally exposes a typed handle wrapper.

The subtype appears in the prebuilt Data Editor's create menu and generic workspace when the
project module loads. No engine or editor source edit.

### Add a locomotion mode

A game module:

1. registers a mode name and active/request tags;
2. registers its session component;
3. registers one locomotion system;
4. defines entry/exit ability data;
5. adds profile mode data.

No engine source edit.

### Replace Free locomotion

The game omits `RegisterFreeLocomotionSystem` and registers its own producer of
`LocomotionOutput`. It keeps facts, profile resolution, transitions, composition, and
the motor.

### Replace the motor

The game keeps ordinary Jolt stepping, omits default character motor registration, and
registers a Physics system consuming `MotionRequest` and writing the same fact contract.

### Replace everything

The game omits the convenience recipe and uses only the lower engine facilities it
chooses. The engine host never auto-installs gameplay movement behind the module's back.

---

## 11. Performance analysis and gates

### Asset path

The refactor adds one registration-table lookup to generic load orchestration, not to
render or simulation hot paths.

- typed handles and direct caches remain;
- staging and commit frequency are asset-load frequency;
- dependency graph work occurs only during load/reload;
- `AssetLease` removes repeated type switches and typed vector storage;
- data assets compile once and bind once per World/version.

### Movement path

At expected scale:

- support changes are field writes, not archetype moves;
- one resolver pass and one locomotion query per relevant mode are negligible;
- modes with sessions filter at archetype level;
- composition is a small fixed set of vector operations;
- no JSON, strings, virtual data-asset dispatch, or asset lookups occur per coefficient;
- volume overlap is the likely expensive movement-side operation and is opt-in/profiled;
- no chunk parallelism is added until the existing profile gate is crossed.

Add measurements:

- movement FixedLogic total;
- resolver time and characters resolved;
- volume overlap count/time;
- character motor time;
- asset stage time;
- asset commit time;
- dependency wait time;
- data binding rebuild count.

---

## 12. Tests and fitness functions

### Asset orchestration

- every built-in kind registers exactly once;
- duplicate type or extension registration fails;
- `AssetLease` releases exactly once across move/reset/destruction;
- generic commit returns a live creation lease;
- typed facades return the same underlying handle as generic load;
- preloader stores heterogeneous leases without concrete type branches;
- dependency graph commits parents after dependencies;
- duplicate dependencies coalesce;
- cycle detection reports the full cycle;
- cancellation releases scaffold leases;
- hot reload dispatches through registration;
- changed dependencies acquire before old dependencies release;
- skeleton, clip, and skinned mesh preload asynchronously without fallback.

### Structured data and Data Editor

- `.sdata` envelope validation;
- unknown subtype and unsupported version errors;
- engine-defined and game-defined subtype registration;
- subtype unregistration refuses resident values and open documents;
- hot reload preserves `DataAssetHandle`;
- typed retrieval rejects the wrong subtype;
- dependency extraction;
- subtype-filtered inspector metadata;
- schema covers records, arrays, optionals, enums, filtered references, tags, defaults,
  descriptions, units, and numeric constraints;
- schema and semantic errors retain exact JSON paths;
- game-defined subtype appears in the generic editor without an editor rebuild;
- create/open/edit/save/reopen round trip is deterministic;
- undo/redo covers scalar edits, array insert/delete/duplicate/reorder, and optional sections;
- error selection focuses the corresponding field;
- invalid semantic save leaves the resident last-known-good value unchanged;
- dirty external-change conflict does not silently discard either version;
- Kettle project and asset deep links launch the correct document;
- movement workspace effective-value trace agrees with the runtime resolver;
- module lifecycle ordering test.

### Movement

- support field changes do not migrate archetypes;
- grounded projection precedes ability activation;
- mode, session, and active tag always agree after transitions;
- at most one mode session exists;
- Forced > Explicit > Automatic;
- same-class first-write-wins;
- explicit entry replaces stale automatic exit;
- profile layer order and set/scale/add semantics;
- per-World binding invalidates on profile reload;
- unknown names fall back once with a diagnostic;
- Free ground and air numerical fixtures;
- moving and rotating support velocity inheritance;
- step height and ground snap fixtures;
- jump and dash compose on independent axes;
- two same-axis overrides resolve deterministically;
- impulses add and are consumed once;
- motor writes collision-corrected velocity;
- flight sustain timing;
- cling capture/loss timing;
- volume overlap reconciliation across entity destruction and World detach;
- game-defined mode with zero engine edits;
- game-replaced Free locomotion;
- game-replaced character motor.

### Diagnostics

- layer trace reproduces final tuning;
- contribution trace reproduces final request;
- disabled diagnostics add no per-frame allocation;
- tag-cap warning fires at a declared threshold.

---

## 13. Migration and observable behavior changes

### Deleted mechanisms

- ground/air mode markers and priority arbitration;
- per-adjective ground/air systems;
- global movement cvar overwrite;
- direct multi-writer `MotionRequest`;
- bespoke movement-profile asset proposal;
- hardcoded asset preloader waves;
- generic loader commit that discards ownership.

### Kept and reshaped

- pure movement math kernels;
- AbilityKit jump data;
- `MoveSpeed` attribute;
- typed asset caches and handles;
- staged loading;
- hot reload in place;
- Jolt PIMPL;
- character mover pool and reconcile;
- game-module composition.

### Intentional behavior deltas

1. Carried velocity becomes collision-corrected. Pushing into a wall no longer preserves
   phantom planar speed.
2. Moving-platform velocity becomes explicit and inherited through support facts.
3. Jump gating remains one tick behind Physics, matching the fixed-tick fact contract.
4. Mode transitions can no longer leave stale active mode tags for a tick.
5. Automatic exits no longer consume fresh explicit input.
6. Jump and planar actions can compose instead of last-writer-wins.
7. Live movement tuning moves from global cvars to hot-reloaded per-character data
   assets.
8. Structured data authoring moves from raw JSON as the expected workflow to a dedicated
   schema-driven editor launched from Kettle; raw JSON remains available as an expert view.
9. Game-defined data subtypes become authorable without rebuilding Sencha's tools.
10. Skeletal dependencies can preload asynchronously instead of relying on synchronous
    fallback.

---

## 14. Implementation slices

Each slice leaves the suite green and deletes temporary compatibility code as soon as
its replacement is proven.

### Slice 1. Generic asset ownership seam

- add `AssetLease`;
- add `IAssetStore`;
- adapt concrete caches;
- change `AssetCommitResult` to return creation ownership;
- adapt all loaders;
- preserve typed load APIs;
- add ownership tests.

### Slice 2. Registered asset-kind orchestration

- add `AssetKindRegistry`;
- register built-in kinds in `RuntimeAssets`;
- move runtime extensions into registrations;
- replace `LoaderFor` and `IsResident` switches;
- remove concrete loader getters from generic clients;
- keep direct typed cache access for domain systems.

### Slice 3. Dependency-aware preload and generic hot reload

- add staged dependency reporting;
- replace typed preload vectors with `AssetLease`;
- replace acquire/commit/deliver switches;
- delete material-specific waves;
- add cycle handling;
- enable skeleton, clip, and skinned mesh async preload;
- route hot reload through registered operations.

### Slice 4. Structured data runtime and authoring schema

- add `AssetType::Data` and `.sdata`;
- add `DataAssetLoader`, cache, handle, and subtype registry;
- add `DataSchemaRegistry` and the v1 field vocabulary;
- add game-module runtime type/schema registration hooks and ABI update;
- add subtype-filtered asset field metadata;
- add structural and semantic validation with JSON paths;
- add hot reload and dependency support;
- ship a small engine test subtype before movement consumes it.

### Slice 5. Dedicated Data Editor and Kettle integration

- add the standalone `data_editor` application over `editor_common`;
- add Kettle launch, create-new shortcuts, recent assets, and `--asset` deep links;
- add schema-driven document model, generated form, documentation pane, and validation
  panel;
- add tabs, dirty state, undo/redo, deterministic save, duplicate/rename/delete, and
  external-change conflict handling;
- add searchable filtered pickers for assets, data subtypes, enums, and gameplay tags;
- add raw JSON expert view over the same document model;
- prove a game-defined subtype is fully authorable with no editor rebuild.

### Slice 6. Motor facts and platforming contract

- add `SupportState`, `KinematicState`, and final `MotionRequest`;
- reshape `CharacterController`;
- reshape `CharacterMover` for full 3D velocity and variable up axis;
- add contact point and support velocity;
- add step, snap, and moving-platform tests;
- temporarily adapt old locomotion to the new motor seam.

### Slice 7. Movement profile data, binding, and editor workspace

- register `movement.profile` data subtype and complete schema metadata;
- add portable compiler;
- add per-World binding cache;
- add `ResolvedMovementTuning`;
- replace tuning cvar overwrite;
- add hot-reload invalidation and resolver diagnostics;
- add layer editing, local condition controls, context simulation, effective-value trace,
  acceleration response, and jump-arc preview to the Data Editor;
- template ships default `.sdata` profiles.

### Slice 8. Modes, support projection, and Free locomotion

- add `CharacterMovement`, transition requests, registry, and atomic transition system;
- project grounded support;
- replace ground/air systems with Free;
- delete markers and priority arbiter;
- retarget jump request flow;
- add transition and numerical tests.

### Slice 9. Motion composition

- add `LocomotionOutput`, axis overrides, impulses, and composer;
- make composer sole writer of `MotionRequest`;
- route jump through up override;
- add dash/ground-pound/impulse fixtures;
- record the root-motion insertion point;
- split registration recipes for replacement.

### Slice 10. Volume sensing, flight, and cling

- add opt-in volume sensing and per-World overlap state;
- add `Immersion`;
- add flight and cling sessions/systems;
- add sustain, auto-exit, surface capture, and detach tests.

### Slice 11. Extensibility, template, diagnostics, and docs

- prove game-defined mode;
- prove Free replacement;
- prove motor replacement;
- update template input to 3D intent and hold tags;
- add movement inspection UI/debug output;
- update Kettle, editor-family architecture, asset architecture, movement docs, and roadmap;
- remove obsolete compatibility code and stale comments.

At every slice boundary, implementation discoveries that invalidate a contract are
recorded before proceeding. Do not hide a bad lower-level seam behind adapters.

---

## 15. Explicit non-goals

- A universal asset cache or generic GPU resource object.
- Dynamic game-defined outer `AssetType` categories before a real second transport class
  exists.
- A generic composite rules engine, predicate registry, or visual rule graph. Movement
  profile `when` fields remain a local schema compiled by `movement.profile`.
- A game-module ABI for custom editor panels or arbitrary subtype UI plugins before a second
  specialized game-defined workspace proves the boundary.
- General asset migration/version upgrade infrastructure beyond rejecting unsupported
  versions.
- Game-module hot reload. The data-type lifecycle is shaped so that future work has an
  explicit drain point.
- A virtual locomotion strategy interface or central mode dispatch table.
- Swim, ladder, vehicle, ledge-mount, rail, or authored traversal modes without a
  shipping consumer.
- A general character constraint solver. Tether and similar abilities contribute
  through action overrides or impulses until a second irreducible constraint proves a
  shared solver.
- Coyote time and jump buffering in this ticket. They require timestamped action intent
  and should not be faked through support-state duplication. The movement contracts leave
  a clean insertion point.
- Fractional slow motion. That is frame-loop work.
- Input action mapping. Movement consumes resolved intent and ability activations.
- Full root-motion production. Composition reserves the explicit contribution stage;
  animation runtime supplies it.
- Chunk-parallel movement queries before profiling crosses the gate.

---

## 16. Open product questions

These refine data or later features and do not block the architecture:

1. Is capsule-versus-volume-bounds immersion estimation sufficient for the first shallow
   versus deep water distinction?
2. Does SINR mist change collision filtering, such as passing through grates or bars?
   If yes, add an explicit collision-policy contribution to the motor request rather
   than a mist-named branch.
3. Can scenes author a non-Free starting mode, or are modes always runtime state?
4. Is glide initially hold-jump-while-airborne?
5. What product name should the dedicated Data Editor use? This does not affect the internal
   `data_editor` target or architecture.
