# Scene composition: prefabs that place, extend and include

A `.sscene` is composed by one resolver, `ResolveSceneComposition`
(`editor/common/src/scene_source/SceneComposition.h`). Kyusu's live projection
and the cook both consume it, so they cannot disagree. The runtime never
composes: it loads the flattened `.smap` the cook writes. Component values
merge through the shared operator (`docs/assets/composition.md`).

## What a document can say

| Member | Version | Meaning |
|---|---|---|
| `entities` | 1, 2 | The document's own entities, by stable id. |
| `instances` | 1, 2 | Placements of other scenes, each under a synthetic root whose id is the placement's. |
| `extends` | 2 | A scene whose entities this one adopts, ids and all, with no wrapper. |
| `includes` | 2 | Fragments: one-entity scenes merged onto the root, or onto the inherited entity `onto` names. |
| `patch` | 2 | Sparse edits to inherited entities, keyed by id: a component member patches, adds the component whole, or removes it as `null`. |
| `remove` | 2 | Read only: folded into `patch` as `<component>: null`. |
| `abstract` | 2 | Only ever extended or included: never cooked, never placed. |

## Format versions

`format_version` is the scene source format (`SceneSourceDocument::Version`,
2). The cooked `.smap` has its own version (`kSmapVersion`, also 2 since it
gained the presentation section); the two move independently. Composition
leaves the cooked format alone: a derived prefab cooks to an ordinary flat map.

- Both versions read. The `extends`, `includes`, `abstract`, `patch` and
  `remove` members in a version-1 file are refused, not kept as unknown.
- The versions differ in one place: an instance patch. Version 1 replaces lists
  whole and writes `null` as a value; version 2 merges keyed lists by key and
  `null` removes.
- **One model.** A placement's `patch` and a derived prefab's `patch` are the
  only spellings the model holds (`SceneInstanceRecord::Patches`,
  `SceneSourceDocument::InheritedPatches`). The parser folds the retired
  members in as it reads: each placement `add` entry is merged into that
  path's patch, and each `remove` name, a placement's or a prefab's, becomes
  `<name>: null` there. A name both patched and added, or patched (or added)
  and removed, is refused with the component and its path. The writer emits
  `patch` only, so a file Kyusu saves never holds `add` or `remove`. A
  component-level `null` removes in either version; within a component a
  version-1 patch still writes `null` as a value.
- A parsed document keeps the version it was read at, so a version-1 file
  round-trips byte for byte, and a version-1 source another document places or
  extends composes by the version-1 rules.
- Kyusu harvests version 2, so `EditorDocument` moves the document it opens to
  version 2 (`UpgradeSceneSourceFormat`) and saves it that way. The cook opens
  the document it cooks the same way. Each instance-patch `null` in an upgraded
  document is logged, since its meaning changes from a value to a removal; a
  harvest never wrote one, so only a hand-written patch can hold it.

## Composition order

1. **Adopt the base.** It is resolved first, with its own composition, and
   its entities become this document's. Its placements are flattened into
   plain entities.
2. **Compose each inherited entity.** The layers are, in order: the base, then
   fragments in include order, then the document's `patch`.
   - A list merges by key where its component declares one
     (`IComponentSerializer::CompositionSchema`; `AbilityGrants` by `key`,
     `GameplayTags` and `Attributes` by `name`). An undescribed list replaces
     whole.
   - `null` removes a member or a keyed element.
   - Two fragments that set one value differently are an `include_conflict`,
     until the document's own patch states the value.
3. **Apply removals.** A component goes by a `null` in place of the component
   in `patch`; an entity's nulls are its one removal list, checked once
   against what stays once the whole list goes. A null naming a component the
   entity does not have is reported as an override that matched nothing
   (`$.patch: <id>.<component>`). Removing a component
   another remaining component owes is `removes_owed`; removing a structural
   one is `not_removable`; either leaves the component in place.
4. **Add the document's own entities and placements.** A local may hang
   beneath an inherited entity.

A version-2 instance patch composes the same way, so a placement can drop an
inherited response binding by writing `bindings.<key>: null`. A placement's
removals, the component `null`s in its patch, are held to the same check after
the rest of its patch applies. A component a placement adds merges into one its
source gains later: the placement's values win field by field and fields it
never stated come from the source (the retired `add` replaced the source's
component whole; no checked-in file used it).

**References between a placement's members.** Each placement mints its own
ids for the source's entities. A component field that names another entity
(`DataFieldKind::Entity` in its `CompositionSchema`) is renamed with them, so
each placement's members name each other. A reference to a member the
placement drops is `dangling_member_reference`.

A fragment states its values itself: one that `extends` another is refused
with `include_extends`, since what it inherits would land as what it wrote. A
fragment goes only onto an inherited entity (`fragment_target`,
`docs/deferred.md`): its values would be saved into a local as the local's own.
A local whose inherited parent the base no longer has is `dangling_parent` and
composes as a root, so the document still opens to be fixed.

The resolver returns nothing on a structural failure: a source reference that
does not resolve (named with the document and field that asked for it), a
cycle through the source graph or a chain of extends, includes and placements
deeper than 16 scenes (`CompositionChain`, `docs/assets/composition.md`,
"Chains of sources"), or a local entity reusing an inherited id, which would
make every patch and parent naming it ambiguous. Without component rules
(`ISceneComponentRules`, whose `EntitySchema` is the serializer registry's entity
schema) every component composes by the version-1 rules and no removal is
checked.

**Cost.** One call resolves each source once: extends, fragments and
placements of a source already resolved in that call copy its result rather
than resolving it again, and the copy is what a placement overrides and emits.
The cache lives for the call and holds one result per distinct source it read,
so it grows with the content of that one resolve and needs no limit of its own;
a reused source is still held to the depth where it is placed again. Component
schemas are gathered into one entity schema per call rather than copied per
entity. Provenance, which only tests read, is built when
`SceneCompositionOptions::Provenance` asks for it. `SceneCompositionBench`
(env-gated by `SENCHA_SCENE_COMPOSITION_BENCH_OUT`) resolves 200 and 2,000
placements of a ten-entity prefab three documents deep; in a debug build the
cache and these changes took it from 404 ms and 3,802 ms to 42 ms and 407 ms.

What the resolver reports:
- composition diagnostics;
- override paths that matched nothing;
- missing placement ids;
- every document it read.

Kyusu shows these. The cook refuses to cook while any of the first three
remain.

## A derived prefab in Kyusu

- **Projection.** Inherited entities are live, under their own ids. Their
  baseline is what they inherit, before the document's own patch.
- **Harvest.** `PatchFrom` over the two component objects, with the registry's
  entity schema, writes an inherited entity's edits as the document's `patch`:
  only what changed, keyed lists by key (whole when their order changed),
  removed elements and cleared members `null`, a component the entity gained
  whole and one it lost as `null` (`docs/assets/composition.md`). Placement
  overrides are harvested the same way into the placement's `patch`. Patch
  entries for ids the projection did not produce carry over. Brush geometry
  never crosses documents through a patch (`IsDocumentLocalComponent`, in
  `SceneInstanceProjection.cpp`): it names ids in its own document's sidecar.
- **Structure.** Deleting an inherited entity deletes only what the document
  authored beneath it. Reparenting one is refused. Both cases are
  `EditorDocument::IsSourced`.
- **Abstract.** Placing an abstract scene is refused with the reason.

Choosing what a scene extends and includes from its card, and showing where an
inherited value came from with revert and restore, are not built yet
(`docs/deferred.md`, "Kyusu authoring for prefab extends").

## Cook and recook

- The cook records the scenes a document composed from as the index entry's
  additional sources, by path.
- A document depends on every source its resolve asked for, found or not
  (`SceneSourceCache::HasRequested`), so creating a missing source rebuilds it.
- Saving a scene in Kyusu re-projects the open documents composed from it,
  and recooks the cooked documents that recorded it
  (`RecookComposedDependents`), on the owner thread, skipped while a cook runs.
- The derived `.smap` keeps the derived scene's asset id, so spawning and
  `NetSpawnPrefab` are unchanged.
- A document's cache key holds the schema fingerprint of every component its
  cooked scene carries, the composed members' and the generated cells' among
  them: the check a runtime makes before it reads the `.smap`. A schema change
  recooks the documents that hold the component instead of leaving a scene the
  runtime refuses with "recook the level".
- A cook given an asset stack (`CookDocument(..., RuntimeAssets*)`) mounts the
  scene it wrote into that stack (`MountCookedFile`): a new `.smap` is
  registered, and a resident one takes the new contents in place. So one cook
  run can cook a prefab and then a level whose data loads it.

The rules and their rationale are in
`docs/plans/composition-stage2-prefab-extends.md`.
