# Composition Stage 1: one composition operator, and data definitions that extend and include

Status: **approved for implementation** (2026-10-15), with the owner's four
corrections applied:
- sparse keyed edits (S1.6);
- reloading dependents of a non-resident source (S1.5);
- how a keyed patch's key is obtained and checked;
- compile errors keep the `SplitCompileError` convention.

**On main:** ported from `claude/composition` onto a later main as module ABI
v48. ABI numbers below are the branch's own. The rules as built are in
`docs/assets/composition.md`; the port also refuses a fragment that extends
(the audit's COMP-S3 rule, applied to definitions as well as prefabs).

Version-1 fact schemas are refused, with no shim.

This is the implementation plan for Stage 1 of
`docs/plans/definition-composition-inventory-equipment.md` (the RFC, approved).
It builds on Stage 0 (`docs/plans/composition-stage0-prerequisites.md`, done).

Nothing here knows about items, equipment or the later AbilityKit work.
Prefab `extends`, fragments on scenes, and keyed scene patches are Stage 2.

Audience: the owner reviewing the plan, and whoever implements it.

Facts were read on branch `claude/composition-stage0` (main 93476b54 plus
Stage 0) unless marked otherwise.

## What Stage 1 delivers

- **A data asset can name a base and fragments:**
  - `extends`: one base of the same subtype;
  - `includes`: fragments of the same subtype;
  - `abstract`: a definition that exists only to be extended or included.
- **The loader composes before it validates and compiles.**
  - Runtime systems see one ordinary compiled value. Nothing is resolved at
    runtime.
  - Kyusu authors a child in place:
    - every row shows its effective value and where it came from;
    - an edit writes a local override;
    - revert, remove and restore are verbs;
    - every error lands on the authored field that caused it.
- **Editing a base updates its dependents**, at runtime and in the editor.
- **One composition operator, in `core`.**
  - Data assets use it now.
  - Scene composition moves onto it now, with today's semantics unchanged, so
    there is never a second merge.
  - Keyed scene patches (version 2) use it in Stage 2.
- **The animation fact schema's own `extends`**, the only other inheritance
  mechanism in the tree, is migrated onto it and deleted.

## Invariants

| Invariant | Owner |
|---|---|
| A composed definition is a pure function of its sources. The same sources give the same value and the same diagnostics, on every machine. | The core composition operator |
| Absent means inherited, a present value is an override (even when equal to the base), and `null` clears. Ambiguity between fragments is an error at the field; the local value resolves it. | The operator, with the RFC's R1–R3 |
| A data asset that does not use `extends` or `includes` loads exactly as it does today. | The shared data-asset staging function |
| An `abstract` definition is never loaded as a value, declared as vocabulary, or offered as a reference target. | Staging, plus the two path enumerations that feed declarations and pickers |
| Every composition, validation and compile error names an authored location: a source asset plus a path in it. | Staging, through the operator's provenance |
| Editing a source recomposes every resident dependent, in dependency order. | `DataAssetCache` reverse dependencies (runtime) and `DataDocumentStore` (editor) |

## Design

### Authored form

The envelope gains three optional members beside `data`. Today the loader and
the editor both ignore unknown envelope members (`DataAssetLoader.cpp:65-84`,
`DataDocument.cpp:338-377`), so no existing file changes meaning.

```json
{ "id": "…", "type": "effect.definition", "version": 1,
  "extends": "asset://effects/armor_base.sdata",
  "includes": [ "asset://effects/fragments/fire_resist.sdata" ],
  "abstract": false,
  "data": {
    "duration": "infinite",
    "modifiers": { "m_armor": { "magnitude": 14 }, "m_speed": null }
  } }
```

- **`extends` and `includes` entries** are asset references in the existing ref
  form: a path, or `{id, path}`, resolved like every data ref
  (`AssetRefCodec.cpp:83-97`, `ResolveAssetRefs`). Each must name a data asset
  of the same subtype and version.
- **`data` is a patch** over the base composed with the fragments:
  - A record member that is **absent** is inherited.
  - A member that is **present** overrides.
  - **`null`** removes the member from the composed record. Removing a
    `Required` member is the error `cannot_clear_required`.
- **Keyed arrays.**
  - An array whose element record has a merge key (below) may be patched as an
    **object from key to element patch**. That merges into the inherited
    element with the same key, or adds a new element when the key is new.
  - `null` removes the inherited element. Removing a key that nothing inherited
    is the error `remove_unknown_key`.
  - Added elements follow inherited ones, in patch order. Inherited order is
    kept.
  - **The key.** In the object form, the map key *is* the element's merge key.
    - An element patch may omit its key member; the composed element takes the
      map key.
    - If it states the key member, the value must equal the map key, else the
      error `key_mismatch`, at that member.
    - A map key that is not a valid key for the member's kind (a `StableKey`'s
      1–64 `[A-Za-z0-9_-]`) is `invalid_key`.
    - In the array form, each element must carry its key member, as schema
      validation already requires of a `StableKey` record. A key repeated in
      the array form is the existing duplicate-key error.
  - The array form replaces the whole array, as today. That is the explicit way
    to reorder.
- **Unkeyed arrays** take only the array form (replace whole). An object patch
  on one is the error `keyed_patch_on_unkeyed_array`.

### Merge keys (a `DataSchema` addition)

`DataFieldSchema` gains two members on `Array` schemas:
- **`MergeKey`:** the name of a member of the element record.
  - It defaults to the element's `StableKey` member, when there is exactly one.
    That covers every keyed list today: ability definitions, effect modifiers,
    attribute declarations, cue presentations, grant entries.
  - It can be set explicitly for arrays keyed by name (the fact schema's
    `slots` and `derived`).
- **`MergePolicy`:**
  - `Override` (default): a patch may change an inherited element.
  - `AppendOnly`: redeclaring an inherited key is the error
    `redeclares_inherited`. This is the fact schema's existing rule, "a schema
    extends by adding names, never by redeclaring one"
    (`AnimRigBinding.cpp:103-111`).

**Not keyed**, so they replace whole: movement profile layers and modes, input
actions and contexts, behavior sets, request schemas. Their order is semantic
or their names are not unique (`MovementProfileData.cpp:184-191,329-330`).
Keying one of them later is a one-line schema change, made when a variant
needs it.

`DataSchema.h` is in the module-facing hashed headers (`core/metadata/*.h`).
`DataFieldSchema` is registration surface games construct, so this changes the
game ABI: `SENCHA_GAME_ABI_VERSION` 28 → 29, with a history note.

### Composition order and conflicts

```text
composed(asset) = Local( Includes( composed(base) ) )
```

1. **The base** is composed recursively.
2. **Each fragment** is composed recursively, then merged onto the base result.
   Fragments do not rank among themselves:
   - two fragments setting one leaf, or one keyed element's leaf, to *different*
     values is `include_conflict`, unless the local patch sets it;
   - equal values are not a conflict.
3. **The local `data` patch** applies last and wins.

**Structural refusals:**
- `composition_cycle`: the stack of sources currently being composed repeats.
- `composition_depth`: more than 16 links.
- `source_missing`: a reference resolves to no asset.
- `source_wrong_subtype` and `source_wrong_version`.

### Provenance and authored locations

**The report.** The operator returns the composed value plus a
`CompositionReport`:
- `Diagnostics`: `{Code, Severity, Path, Source, SourcePath, Message}`, where
  `Path` is in the asset being composed;
- `Provenance`: for every composed leaf and keyed element, which layer set it
  (`Base`, `Include`, `Local`), the source asset, and the path in that source;
- `Removed`: keyed elements and members the local patch removed, so the editor
  can show them;
- `Sources`: every asset the composition read.

**How it is built.**
- Paths use the existing syntax (`$.a.b[3].c`, `DataSchema.cpp:22-26`).
- A keyed element's composed index maps back to its key, and the key to its
  index or map entry in each source.
- Provenance is computed only when asked for. The runtime loader asks for the
  diagnostics only.

**Mapping errors.** Validation and compile run on the composed value, so their
error paths are composed paths.
- Schema errors carry a path (`DataValidationError::Path`).
- Compile errors keep the existing convention: a compiler that knows the field
  leads its message with a `$.`-path, which `SplitCompileError` separates
  (`DataDocument.cpp:430-452`). That helper moves to the shared staging
  function, so the loader and the editor split the same way.
- A compile error with no leading path is reported against the definition
  itself (`$.data`), with no field precision invented.

Staging maps each path through provenance to the authored location:
- an error at a value the child set lands on the child's field;
- an error at an inherited value lands on the child, reading
  "inherited from `<source>` at `<path>`".
- Example: `$.data.modifiers.m_armor.magnitude (inherited from
  asset://effects/armor_base.sdata at $.data.modifiers[0].magnitude): must be
  at least 0`.

### Abstract definitions

**Staging an abstract asset:**
- it composes (so its own chain and conflicts are checked);
- it validates in a partial mode: kinds, ranges and keys are checked, required
  members are not (`ValidateDataAgainstSchema` gains that option);
- it skips the subtype compile.

**Loading:** an abstract asset loaded as a value fails with `abstract_target`:
"'X' is abstract; extend or include it". An ordinary data reference that names
one fails the referring asset's load at that field, through the existing
dependency failure path (`DataAssetLoader.cpp:133-170`).

**Hiding abstract assets.** The two path enumerations skip them:
- `ContentVocabulary`'s `PathsOfSubtype` (`ContentVocabulary.cpp:40-48`), so an
  abstract effect or ability is never declared;
- Kyusu's `DataDocumentStore::DataAssetPaths` (`DataDocumentStore.cpp:210-218`),
  so pickers do not offer it, except the `extends` and `includes` pickers.

A peek beside `PeekDataAssetSubtype` (`DataAssetSubtype.cpp:11-29`) reads the
flag.

### One staging function, two source readers

**The duplication today.** The runtime stages a data root in
`DataAssetLoader::StageRoot` (`:49-131`). The editor re-implements the same
envelope, schema and compile checks in `DataDocument::Validate` (`:338-411`),
with its own path rebasing.

**One function.** Stage 1 replaces both with one function in
`assets/data/`: `StageDataDefinition(root, reader, current)`. It:
1. checks the envelope;
2. composes through the operator;
3. validates the composed value (partial if abstract);
4. compiles (unless abstract);
5. maps every error to an authored location.

Both callers use it, and the editor's copy is deleted.

**What composition needs.** It needs the *source* of a base or fragment, not
its compiled value, so the function takes a source reader:
`std::function<const JsonValue*(std::string_view path)>`, owned by the caller.

| Caller | Reader |
|---|---|
| Runtime staging (worker thread) | Reads the source through `IAssetSource`, given a path→record snapshot published the way `AssetRegistry::PathsById` already is (`AssetRegistry.h:58-61,90`). Today the stage half has only the id→path snapshot, so the snapshot is new. |
| Kyusu (owner thread) | `DataDocumentStore::CurrentRoot`: the open document's working copy, else the file (`DataDocumentStore.cpp:197-208`). A child therefore composes against its base's unsaved edits. This is already used across documents (`InputProfilePreview.cpp:55`). |
| `DataResidentSync` (owner thread, pushing editor edits to the runtime) | The same editor reader, so the runtime value matches what the sheet shows. |

**Where composition happens.** At stage time. A base is not loaded as a
runtime dependency: it may be abstract, and the composed child no longer needs
it. Its path is recorded as a **composition source** instead.

### Reloading dependents

**Runtime.**
- `DataAssetCache` entries record their composition sources, and the cache
  keeps the reverse index. That is the first reverse dependency for data
  assets; today there is none.
- **The reverse index is keyed by source path, not by residency.**
  - When `AssetHotReloader::ReloadSource` sees a `.sdata` change
    (`AssetHotReloader.cpp:126-154`), it consults the reverse index for that
    path whether or not the changed asset is itself resident.
  - It re-stages the changed asset only if it is resident, and re-stages every
    resident asset whose sources include it, directly or through a chain.
  - This matters because an abstract base is never resident (it cannot load as
    a value), yet concrete children depend on it.
  - Today `ReloadSource` returns early for a registered asset that is not
    resident; this is the change.
  - The closure is staged in dependency order (bases first), so each dependent
    composes the new base.
  - Each bumps its `ReloadVersion`, which every existing consumer already
    watches (`RefreshContentVocabulary`, `DataAssetStamp`, `AnimRigBindings`).
- A dependent that fails to recompose keeps its last good value, which is
  today's reload-failure behaviour, and reports at its authored location.

**Editor.**
- `DataDocumentStore` keeps the same reverse index over open documents.
- When a document changes (`Changed()`, `DataDocumentStore.cpp:241-247`), its
  open dependents revalidate, republish their problems, and push through
  `DataResidentSync`.

### Kyusu: authoring a child in place

**Envelope rows**, at the top of the data sheet:
- *Extends* is an asset picker over same-subtype assets, abstract ones
  included.
- *Includes* is a list of the same.
- *Abstract* is a toggle.

**Effective rows.** `DataProperties` lays out the **composed** value; today it
lays out the working root (`DataProperties.cpp:353-384`). For each row:

| Situation | Row | Verbs |
|---|---|---|
| Set here | value; detail "set here" | *Revert to inherited* when a source supplies one (erases the local member) |
| Inherited | value; detail "inherited from `<base>`", or "from `<fragment>`" | editing writes a local override |
| Keyed element inherited | the element card; detail "inherited from …" | *Remove* (writes `key: null`) |
| Keyed element removed here | a struck-through, read-only card; detail "removed here" | *Restore* (erases the null) |
| Include conflict | value from neither fragment; a problem note naming both | *Take `<fragment>`'s value* (writes it locally) |

- **Presentation.** Provenance is prose in the row's detail line, the idiom the
  movement resolve view already uses ("set by `<layer>`",
  `MovementPreviewProperties.cpp:115-131`). `PropertyDescriptor` gains no new
  presentation kind; `Tooltip` already maps to the row's `Detail`
  (`PropertySheet.cpp:117-215`).
- **Keyed elements are addressed by key**, not index, when a source is
  involved. `DataProperties::Locate` gains key steps, because index steps fail
  for an element the local array lacks (`:89-91`).
- **An edit writes the sparsest local patch.**
  - Editing one field of an inherited element writes a map-form entry holding
    only that field and the records on its path. For example, editing `damage`
    in an inherited op's arguments writes
    `{ "o_hit": { "arguments": { "damage": … } } }`.
  - Sibling fields stay inherited, so a later base change to them still
    reaches the child.
  - The same holds for nested records outside keyed arrays: `Locate` creates
    missing *local* records empty, never seeded from the effective value or
    the schema default.
  - Only a member that is absent from both the local patch and every source
    is created from its default, as today (`DataProperties.cpp:105-111`).
  - *Revert to inherited* erases the local member. When that empties a local
    record or map entry, the empty container is erased too, up to the first
    ancestor that still holds something. A revert therefore leaves the
    document as if the field had never been edited.
- **Problems.** `DataWorkspace::RevealProblem` focuses the field
  (`WorkspaceSelectionFacet::Focus`), as the Ability Workbench does
  (`AbilityWorkspace.cpp:638,670`). Today it only opens the document
  (`DataWorkspace.cpp:106-113`).

### The fact schema migration

**Today:**
- `extends` is a `data` field.
- The binder walks the chain at bind time: depth 8, `anim.fact.extends_cycle`
  and `anim.fact.duplicate_slot` (`AnimRigBinding.cpp:65-139`).
- The rig outline walks it again (`AnimationRigOutline.cpp:34-46`).

**After:**
- `extends` is the envelope's.
- `slots` and `derived` have `MergeKey = "name"` and `MergePolicy =
  AppendOnly`.
- The binder reads one composed schema, and its chain walk and both diagnostics
  are deleted. Their cases become the operator's `composition_cycle`,
  `composition_depth` and `redeclares_inherited`, at the same authored fields.
- The outline reads the composed schema with provenance.
- The subtype goes to version 2. The three in-tree files move `extends` to the
  envelope:
  - `engine/assets/animation/engine.facts.sdata`;
  - `test/fixtures/content/assets/animation/hero.facts.sdata`;
  - `…/examples/examples.facts.sdata`.
- A version-1 file is refused by the existing version check, with a message
  that says what moved.
- No fact schema exists outside this repository (checked:
  `../loss-function-sencha-abilitykit`).

This removes the only other implementation of definition inheritance.

**Behaviour it keeps:**
- base slots first;
- a redeclared name refused;
- a base edit rebinding the rig. That now happens through reload dependents and
  `ReloadVersion`, which `AnimRigBindings::IsCurrent` already watches
  (`AnimRigBinding.cpp:459-465`).

### Scene composition moves onto the operator, unchanged

**The merge today.** `SceneComposition.cpp`'s `MergeInto` (`:52-68`, editor-only,
on `Json5Value`) is the only JSON merge in the tree:
- objects merge member by member;
- anything else replaces whole;
- `null` is written as a value.

**The move.**
- The operator is a template over a small value-access concept, so it runs on
  `JsonValue` and on the editor's `Json5Value`, comments included.
- Scene composition calls it with the **version-1 policy**: no merge keys, and
  `null` written as a value. `MergeInto` is deleted.
- Patch semantics do not change, so the harvest diff (`SceneInstanceProjection.cpp:39-103`),
  which can only round-trip whole-array replacement, stays as it is.
- Keyed scene patches, tombstones in the harvest, version 2, and the three
  custom components' merge keys are Stage 2.

**Pinning first.** No test pins `MergeInto`'s array or `null` behaviour today.
Stage 1 adds the pins before it moves anything.

## Steps

Each step is a commit, with its tests written first. Where a step changes
existing behaviour, the old behaviour is pinned first.

| Step | Work | Tests |
|---|---|---|
| **S1.1 Operator** | `core/composition/`: the operator over a value concept with `JsonValue` and policy inputs (schema node or none, null handling); diagnostics; provenance; removed set. `JsonEquals` in `core/json` (there is no JSON equality in the tree). | `test/core/CompositionTests.cpp`, table-driven over every rule above: absent/override/null; keyed add, merge, remove, reorder-by-replace; an element patch omitting its key, stating the matching key, and stating a different key (`key_mismatch`); an invalid map key; unknown removal; keyed patch on an unkeyed array; append-only; include agreement and conflict; local resolution of a conflict; cycle; depth; provenance of each layer; composed→authored path mapping. |
| **S1.2 Scene merge** | Pin `MergeInto`: arrays replace whole, `null` written, new members appended in order, comments carried. Then `Json5Value` access for the operator; scene composition calls it with the version-1 policy; delete `MergeInto`. | The pins, written and passing on the old code; `SceneCompositionTests`, `SceneProjectionTests` and `SceneSourceDocumentTests` unchanged and green. |
| **S1.3 Schema** | `MergeKey` (defaulted from `StableKey`), `MergePolicy`; partial validation; ABI 29 and its history note. | `test/core/DataSchemaMergeTests.cpp` (new, beside `StableKeyValidationTests.cpp`): key defaulting, explicit key, partial mode. |
| **S1.4 Staging** | `StageDataDefinition`, with `SplitCompileError` moved into it; envelope members; abstract handling; source readers (runtime snapshot, editor store); `PathsOfSubtype` and `DataAssetPaths` skip abstract; `DataAssetLoader::StageRoot` and `DataDocument::Validate` call it, and the editor's copy is deleted. | Runtime (`DataAssetDependencyTests` style): a child loads composed; a base edit is not visible until reload (next step); abstract refused as a value; a reference to an abstract asset fails at its field; every structural diagnostic at its authored location. **Compatibility:** every `.sdata` in the tree stages to the same compiled result and the same errors as before. The gate is an equivalence test over the content roots. |
| **S1.5 Reload** | Composition sources on cache entries, the reverse index (keyed by source path, independent of residency), closure reload; editor reverse index and revalidation. | Edit a base: resident dependents recompose base-first and bump `ReloadVersion`; a broken dependent keeps its last value. **Regression:** a non-resident abstract base changes while a concrete child is resident. The child recomposes, its `ReloadVersion` moves, and when the edit breaks it, the child keeps its last good value and reports at its authored location. A two-level chain (abstract base → abstract middle → concrete child) recomposes through the middle. Editor: an open child's problems update when its open base changes. |
| **S1.6 Kyusu** | Envelope rows; composed layout with provenance; key-addressed `Locate`; sparse writes; revert, remove, restore and take-value verbs; reveal focuses the field. | `DataPropertiesTests`: rows for each situation in the table above; edits write the minimal local patch (scalar, nested field in a keyed element, removal); undo and redo restore the exact local document; revert leaves no empty records behind. **Sparse inheritance:** a child overrides one nested field of an inherited keyed element; the base then changes a sibling field; the child's composed value shows the base's new sibling and keeps its own override. The same holds after undo and redo of the child's edit, and after a revert, which restores full inheritance. |
| **S1.7 Fact schema** | Migration as above. | `AnimFactTests` keeps its three behaviours (base first, duplicate refused, rebind on base change), now through composition. A cycle and the depth limit are tested, which neither is today. Version 1 is refused with the migration message. |

**Gate for Stage 1, on non-item consumers only:**
- an ability variant that extends an ability and patches one op's argument by
  key;
- an effect variant that removes one inherited modifier;
- a cue presentation that includes a fragment;
- the fact schema chain.

Each loads, reloads with its base, and authors in Kyusu with correct
provenance.

## Compatibility

| Content | After Stage 1 |
|---|---|
| A `.sdata` without `extends`/`includes`/`abstract` | Identical: no composition runs. Equivalence test in S1.4. |
| Scenes and prefabs (`format_version` 1) | Identical: the operator runs with today's semantics. Pins in S1.2. |
| Fact schemas | Version 2. In-tree files migrated in the same commit. Version 1 refused with a message naming the move. |
| Game modules | Rebuild for ABI 29 (`DataFieldSchema` members). |
| The wire, cooked formats, saves | Untouched. Data assets are not cooked (`AssetHotReloader.cpp:128-135`). |

## Change-path checklist

| Path | Applies | Notes |
|---|---|---|
| Assets | Yes | Staging, references, reload closure, editor preview, diagnostics for missing and invalid sources. One loading path: the editor's duplicate envelope check is removed. |
| Editor interaction | Yes | Data sheet rows and verbs through the existing document transaction; undo and redo. |
| Public SDK or module boundary | Yes | `DataFieldSchema` members → ABI 29. |
| Persisted formats | Yes | New optional envelope members; fact schema version 2 with in-tree migration. |
| ECS, runtime and rendering, world partition, concurrency | No | Runtime staging reads sources on the existing stage task, through an immutable snapshot. |

## Performance

- Composition is load-time and reload-time work, linear in the composed size
  plus its sources.
- The runtime loader does not compute provenance.
- Nothing runs per frame.
- Measuring is unnecessary unless a chain exceeds the depth limit, which is
  refused.

## Decision for the owner

**Fact schema compatibility.** The plan refuses version-1 fact schemas after
migrating the three in-tree files. That follows the pre-1.0 rule of removing
rather than deprecating, and nothing outside this repository authors fact
schemas.

The alternative is to read version 1's in-`data` `extends` as an envelope
`extends` on load. That keeps a second spelling alive.

**Recommendation:** refuse.

## Deviations from the RFC, named

1. **Keyed scene patches and scene format version 2 move to Stage 2.** The RFC
   listed scene composition consuming the operator in Stage 1, with version-1
   fixtures.
   - Stage 1 does move the scene merge onto the operator, so there is one
     merge.
   - Keyed scene patches cannot land without the harvest diff writing them
     (`SceneInstanceProjection.cpp:39-59` cannot produce a tombstone) and the
     custom components declaring their keys. Both belong with prefab `extends`.
2. **Merge keys are declared on the array schema** (`MergeKey`, `MergePolicy`),
   not only taken from `StableKey`. The fact schema's name-keyed lists need
   this, and so will the three name-keyed scene components in Stage 2.

## Deviations from this plan, named during implementation

1. **A runtime definition with sources composes when it commits (S1.4).**
   Composition runs on the owner thread, not the staging worker. A stager
   receives only the id-to-path snapshot, so a worker cannot read another
   definition's source. A caller that holds a reader, such as Kyusu's resident
   sync, composes at once.
2. **Dependents reload in path order, not bases first (S1.5).** A dependent
   composes from its sources' files, not from their resident values, so the
   order of reloads cannot change what any of them composes to.
3. **Kyusu keeps no separate reverse index (S1.5).** Each open document
   records the sources it composed from, and a change revalidates the open
   documents whose sources name it. Open documents are few, and a second index
   would be state to keep in step.
   - Gap: a base that is not open, changed on disk, does not revalidate an
     open child in Kyusu, because the registry cannot map a file back to its
     asset. The runtime hot reloader still reloads the resident child.
4. **A removed element is shown read-only and not struck through (S1.6).** The
   row's detail reads "removed here". `PropertyDescriptor` gains no
   presentation kind, as this plan requires.
5. **"Take value from" offers the definition's includes (S1.6).** Choosing one
   reads that fragment's composed value at the row. The composer's conflict
   diagnostic names the two fragments in its message, not as data, so the
   operator is unchanged.
6. **An element this definition adds can be renamed (S1.6).** Editing its key
   re-keys its patch entry, which keeps the map key and the stated key in
   agreement. An inherited element's key is read-only. Without this, a child
   could add an element but never name it.
7. **Staging reports what the sources compose to alone (`Inherited`)** when
   provenance is asked. Kyusu uses it to decide whether a row can revert and
   to show what was removed.
8. **The envelope rows are their own property source**
   (`DataEnvelopeProperties`), a section above the fields, keyed by the
   envelope paths that staging reports problems at.
9. **Inherited fact diagnostics land on the schema the rig names (S1.7, owner
   ruling).** The binder reads one composed fact schema. `anim.fact.unprovided`
   and `anim.fact.provider_kind` keep their severity and name the slot by key
   (`$.data.slots{Dead}`). Kyusu's problem says which definition the fact came
   from, and its row reads as inherited. There is no runtime provenance store.
10. **A version's migration note is registration data.**
    `DataAssetTypeRegistration::Migration` is appended when a file older than
    the subtype's version is refused. There is no subtype-specific branch.
11. **A base that is not open, changed on disk, recomposes the open children
    in Kyusu.** The hot reloader leaves a dependent the host holds to the
    host (`AssetHotReloader::SetHostHeld`), so an open child's working version
    is never replaced by its file. This closes the gap named in item 3.
12. **The Ability Workbench does not take a variant.** A composing ability
    shows in the document sheet as it composes. Authoring its patched argument
    there, and the workbench learning composition, are tracked in
    `docs/deferred.md`.
13. **Owner-thread composition cost is tracked, not fixed.** The measurements
    and the trigger are in `docs/deferred.md`.
14. **The shell reveals a problem in a document no workspace has open.** The
    live check found that the shell asked only workspaces that already owned a
    problem's document. A rig problem in a fact schema not yet open was
    therefore never revealed, though the animation workspace opens one when
    asked. `RevealProblemIn` asks owners first, then any workspace that can open
    the document. This predates Stage 1 and was fixed because it blocked the
    gate.
