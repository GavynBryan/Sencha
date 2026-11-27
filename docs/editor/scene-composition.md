# Scene composition: prefabs that place, extend and include

A `.sscene` is composed by one resolver, `ResolveSceneComposition`
(`editor/common/src/scene_source/SceneComposition.h`). Kyusu's live projection
and the cook both consume it, so they cannot disagree. The runtime never
composes: it loads the flattened `.smap` the cook writes. Component values
merge through the shared operator (`docs/assets/composition.md`).

## What a document can say

| Member | Meaning |
|---|---|
| `entities` | The document's own entities, by stable id. |
| `instances` | Placements of other scenes, each under a synthetic root whose id is the placement's. |
| `extends` | A scene whose entities this one adopts, ids and all, with no wrapper. |
| `includes` | Fragments: one-entity scenes merged onto the root, or onto the inherited entity `onto` names. |
| `patch` | Sparse edits to inherited entities, keyed by id: a component member patches, adds the component whole, or removes it as `null`. |
| `abstract` | Only ever extended or included: never cooked, never placed. |

A placement says what it changes with the same `patch`, keyed by the path of
the source entity it edits, beside `add_entities` and `suppress`, which edit
the list of entities rather than an entity.

```json5
{ format_version: 3, extends: "asset://prefabs/guard.sscene",
  patch: { "00000000000000a1": {
      Attributes: { attributes: { Health: { base: 200 } } },   // change
      Light: { color: [1, 0, 0], intensity: 4 },               // add: the base has no Light
      AudioSource: null } },                                   // remove
  instances: [ { id: "0000000000000200", source: "asset://prefabs/door.sscene",
      entity_ids: { "0000000000000001": "0000000000000201" },
      patch: { "0000000000000001": { responses: { bindings: { struck: null } }, Shadow: null } } } ] }
```

## Format versions

`format_version` is the scene source format (`SceneSourceDocument::Version`,
3). The cooked `.smap` has its own version (`kSmapVersion`, 2 since it gained
the presentation section); the two move independently. Composition leaves the
cooked format alone: a derived prefab cooks to an ordinary flat map.

- **Only 3 reads.** `ParseSceneSource` refuses 1 and 2 with
  `format_version 1 is retired: run kyusu_batch --upgrade-scenes on the
  project`, the way it refuses the retired `.level.json` shape. There is no
  upgrade in memory: Kyusu, the cook and `SceneSourceCache` all read through
  the parser, so a file at an old version does not open, cook or place until
  the tool has rewritten it.
- **One spelling.** A component edit is a member of `patch`, at both levels
  (`SceneInstanceRecord::Patches`, `SceneSourceDocument::InheritedPatches`).
  The members format 2 also read, a placement's `add` and `remove` and a
  prefab's `remove`, are refused with what to write instead.
- **What changed from 2.** Format 2 read `add` and `remove` as other spellings
  of a patch, and format 1 also composed a placement's patch without component
  schemas, replacing lists whole and writing a `null` inside a component as a
  value. A component a placement once wrote under `add` replaced the source's
  whole; written in `patch` it merges into one its source gains later, the
  placement's values winning field by field (no checked-in file used `add`).

## Upgrading a project

`UpgradeSceneSourceText` (`editor/common/src/scene_source/SceneSourceUpgrade.h`)
rewrites one file's text at format 3, and `kyusu_batch` runs it over a
project:

```sh
kyusu_batch --project <directory> --upgrade-scenes   # rewrite every old scene, one line per file
kyusu_batch --project <directory> --check-scenes     # change nothing; list each scene not at 3, fail if there is one
```

Both walk every `.sscene` beneath the directory, skipping hidden directories
(`.git`, `.cooked`) and CMake build trees (a directory holding
`CMakeCache.txt`). Each file is reported `upgraded` (with what was folded),
`current` or `refused` (with why); a refused file is left as it was and makes
the command fail. Running either again changes nothing.

- The version becomes 3. A file with nothing to fold changes in that one
  number and nowhere else, whatever its style: the tool edits the number in
  place and checks the result reads back as the same document.
- From 1 or 2, each `add` entry is merged into that path's `patch` and each
  `remove` name becomes `<name>: null` there. A file that folds is written in
  the canonical style (`Json5Write`), comments kept.
- Refused, and named: a name both patched and added, or stated and removed; a
  `null` inside a component of a format 1 placement patch or of any `add`,
  whose meaning would change from a value to a removal; a format 1 file using
  members only format 2 had; anything the result would fail to read.

**Running it on a branch.** The cutover restamps one line near the top of each
scene, so a branch that edited a scene elsewhere merges cleanly. For each live
branch, in each repository (engine and game alike):

1. Merge or rebase onto the main that holds the cutover. Where a scene file
   conflicts, take the branch's side of that file whole.
2. Build `kyusu_batch` from the engine at the cutover (the one engine build).
3. `kyusu_batch --project <repo root> --upgrade-scenes`. Read the report:
   every line should say `upgraded` or `current`; a `refused` line names why.
4. `kyusu_batch --project <repo root> --check-scenes` exits zero. Commit.

A branch that carries its own scene writer changes the number it writes in the
same commit: a generator or script that writes `format_version: 1` or `2`
produces files the parser refuses.

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

An instance patch composes the same way, so a placement can drop an inherited
response binding by writing `bindings.<key>: null`. A placement's removals, the
component `null`s in its patch, are held to the same check after the rest of
its patch applies. A component a placement adds merges into one its source
gains later: the placement's values win field by field and fields it never
stated come from the source.

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
schema) every component composes with no schema: objects merge, anything else
is written whole, `null` removes, and no removal is checked.

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
