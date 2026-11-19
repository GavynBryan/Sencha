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
| `patch` | 2 | Sparse edits to inherited entities, keyed by id. |
| `remove` | 2 | Inherited components this document drops, keyed by id. |
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
3. **Apply removals.** Removing a component another remaining component owes
   is `removes_owed`; removing a structural one is `not_removable`. A removal
   list is checked against what stays once the whole list goes.
4. **Add the document's own entities and placements.** A local may hang
   beneath an inherited entity.

A version-2 instance patch composes the same way, so a placement can drop an
inherited response binding by writing `bindings.<key>: null`.

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
(`ISceneComponentRules`) every component composes by the version-1 rules and no
removal is checked.

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
- **Harvest.** `DiffSceneComponents` writes an inherited entity's edits as the
  document's `patch`: only what changed, keyed lists by key (whole when their
  order changed), removed elements and cleared members `null` (`PatchFrom`,
  `docs/assets/composition.md`). Dropped components become `remove`. Placement
  overrides are harvested by the same diff. Patch entries for ids the projection
  did not produce carry over. Brush geometry never crosses documents through a
  diff (`IsDocumentLocalComponent`): it names ids in its own document's sidecar.
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

The rules and their rationale are in
`docs/plans/composition-stage2-prefab-extends.md`.
