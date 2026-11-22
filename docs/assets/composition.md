# Composition

One operator composes authored values: a composed base, then fragments, then a
local patch. Data definitions compose through it when they load and when Kyusu
lays them out, and scene instance patches apply through it. Nothing composes at
run time: systems see one ordinary compiled value.

The operator is `ComposeLayers` in `core/composition/Composition.h`, a template
over a small value-access trait (`CompositionValueTraits`). `JsonComposition.h`
implements the trait for `JsonValue`; the editor's `Json5Composition.h` does it
for `Json5Value`, comments included. `ApplyCompositionPatch` applies one patch
as a local layer. `ComposeLayers` also hands back, when asked, the value before
the local layer, which is what a child inherits, so no caller composes twice.

## Rules

```text
composed = Local( Includes( composed(base) ) )
```

- **Absent inherits, present overrides.** A member the patch leaves out keeps
  the inherited value. A member it states overrides, even when equal to the
  inherited value.
- **`null` removes,** under `CompositionNull::Removes`. Removing a required
  member is `cannot_clear_required`. Under `CompositionNull::IsAValue`
  (version-1 scene instance patches) `null` is written as a value.
- **Records merge member by member.** Anything else is written whole.
- **Keyed arrays** merge by key. An array is keyed when its schema names a
  `MergeKey`, or when its element record has exactly one `StableKey` member.
  - A patch may be the **map form**, an object from key to element patch. It
    merges into the inherited element with that key, or adds an element when
    the key is new. `null` removes the inherited element; a key nothing
    inherited is `remove_unknown_key`. Added elements follow inherited ones in
    patch order.
  - In the map form an element patch may omit its key member; if it states it,
    it must equal the map key (`key_mismatch`). A map key that is not a valid
    `StableKey` is `invalid_key`.
  - The **list form** replaces the whole list, which is how a patch reorders.
    Each element states its key (`missing_key`).
  - `DataMergePolicy::AppendOnly` refuses a patch that changes or removes an
    inherited key (`redeclares_inherited`, `removes_inherited`).
- **Unkeyed arrays** replace whole. A map-form patch on one is
  `keyed_patch_on_unkeyed_array`.
- **Fragments do not rank.** Two fragments writing different values where their
  writes overlap (one leaf, or a whole record and one of its members) is
  `include_conflict`, unless the local patch writes that path or an ancestor.
  Equal values are not a conflict.

Without a schema, objects merge and anything else is written whole.

## Chains of sources

Every walker over sources, data definitions and scenes alike, keeps the
sources it is resolving in one `CompositionChain`
(`core/composition/CompositionChain.h`), so the two share one rule:

- A source met again while it is still being resolved is a cycle, refused with
  every source on it (`source cycle: 'a' composes from itself through a -> b -> a`).
- A chain is at most `CompositionChain::MaxDepth` (16) sources deep, the
  outermost included. A deeper one is refused, naming the source that went
  past it. The limit bounds the walkers' recursion; no authored chain comes
  near it. A data definition reports the refusal at the field that named the
  source; a scene fails to resolve with it.

## Patches from a difference

`PatchFrom(baseline, live, schema)` (`core/composition/CompositionPatch.h`) is
the operator's inverse: composed over `baseline` as a local patch, under
`CompositionNull::Removes`, its patch gives back `live`, member order aside.
Nothing when the two are equal. Kyusu's harvest uses it, and so does the
entity patch over a whole entity's record (`docs/gameplay/saves.md`, "The
entity patch").

- Members that differ are stated, members `live` lacks are `null`, and a
  record recurses.
- A keyed list is written in the map form when that composes back exactly:
  every element of both lists states a key the list accepts, no key repeats,
  and the elements `live` kept from `baseline` come first, in `baseline`'s
  order. Otherwise it is written in the list form, which replaces the list, so
  a new order, a repeated key or an inserted element survives.
- An append-only list states only its new elements, in the list form, so a key
  the map form would refuse is still added.

`ComposeLayers` reaches some values no patch can state. `PatchFrom` still writes
its best patch for them, and composing it reports what it could not keep:

- an element of a keyed list without its key (`missing_key`);
- an append-only list whose inherited entries changed, went or moved
  (`redeclares_inherited`, `removes_inherited`);
- a required member that is gone (`cannot_clear_required`).

One difference passes unreported: a member that is `null` in `live` and not in
`baseline`, since a `null` in a patch removes. No serializer writes `null`
(`JsonWriteArchive` has no null field), so neither a harvest nor a saved body
meets it.

The property is tested over generated baseline and live pairs, for both value
traits (`CompositionPatch.EveryTargetComposesBackFromItsPatch`,
`Json5Composition.EveryTargetComposesBackFromItsPatch`).

Both traits compare values alike: object members by name in any order, list
elements in order (`JsonEquals`, `Json5Value::SameValueAs`).

## Paths and provenance

Logical paths name a value the same way in every source: `$`, then `.member`,
`[index]` inside an array that replaces whole, and `{key}` for a keyed element
(`$.mods{m_armor}.magnitude`). `CompositionLogicalPath` turns an index path, as
schema validation writes it, into the logical path in a composed value.

When asked (`CompositionPolicy::Provenance`), the report records which layer and
which asset set every composed leaf and keyed element, an empty record
included, and what the local layer removed. A source's own provenance passes
through, so an origin names the asset that authored a value, not the one that
handed it on. The runtime loader asks for provenance only to locate the errors
of a definition that failed.

## Data definitions

A data definition's envelope may name a base and fragments beside `data`:

```json
{ "type": "effect.definition", "version": 1,
  "extends": "asset://effects/armor_base.sdata",
  "includes": [ "asset://effects/fragments/fire_resist.sdata" ],
  "abstract": false,
  "data": { "modifiers": { "m_armor": { "magnitude": 14 }, "m_speed": null } } }
```

- `extends` and `includes` are asset references in the usual form, resolved
  through the build's id-to-path snapshot. Each names a definition of the same
  subtype. `data` is the local patch.
- A fragment may not extend (`include_extends`): it would bring what it
  inherits as though it had written it, ranking that over the base and
  conflicting falsely with other fragments. A fragment may include fragments.
- Composition is refused, at the envelope field that named the source, for a
  cycle, a chain deeper than 16 definitions, a source that does not exist, is
  not valid JSON or does not compose, and a source of another subtype. A cycle
  names every definition on it; a source that does not compose gives every one
  of its errors.
- An `abstract` definition composes and validates partially (kinds, ranges and
  keys, not required members, `DataValidationMode::Partial`), and is never
  compiled. It does not load as a value, is never declared as vocabulary, and
  pickers do not offer it, except for `extends` and `includes`.
- A definition with none of these members loads exactly as before.

`StageDataDefinition` (`assets/data/DataDefinitionStaging.h`) is the one path:
it checks the envelope, composes, validates, compiles and names every error by
an envelope path, with keyed elements named by key and inherited values by the
asset they came from. The runtime loader and Kyusu's `DataDocument` both stage
through it. It reads sources through a `DataSourceReader`: the file at run time,
the open document's working copy in Kyusu.

A runtime definition with sources composes when it commits, on the owner thread:
a staging worker receives only the id-to-path snapshot and cannot read another
definition's source. The loader keeps the parsed sources it read, keyed by
path and content hash: a commit still reads each source's bytes, so an edited
file is never stale, but parses it only when the hash changed, so variants of
one base parse that base once. It holds at most
`DataAssetLoader::ParsedSourceCapacity` (64) between commits; past that, the
source read longest ago is let go and costs one parse when next read. Nothing
is reported, since nothing is lost. Each cache entry records the definitions it composed from,
and `DataAssetCache::ComposedFrom` answers which resident definitions a path
feeds. The hot reloader consults it for every changed file, resident or not, so
editing an abstract base reloads its concrete children. A child that fails to
recompose keeps its last good value and reports at its authored location. An
asset whose version the host keeps itself, such as a definition open in Kyusu
(`AssetHotReloader::SetHostHeld`), is not reloaded from its file when a source
changes: the host recomposes its working copy.

## Authoring in Kyusu

A definition that composes is laid out as it composes (`DataProperties` over
`DataDocument::ComposedData`), with `DataEnvelopeProperties` above the fields
choosing what it extends and includes and whether it is abstract.

- A keyed element is addressed by its key (`$.data.modifiers{m_armor}`,
  `DataPath`), so a row exists for an element the local data lacks.
- Each row's detail says where its value came from: set here, inherited from
  the base, or from a fragment.
- An edit writes the sparsest local patch (`DataPatch`): editing one field of
  an inherited element writes a map-form entry holding only that field and the
  records on its path, so a later change to a sibling in the base still reaches
  the child. Only a member absent from the patch and every source is created
  from its default.
- Revert to inherited erases the local member and every record or keyed entry
  it leaves empty. Remove writes `key: null`; restore erases that `null`; a
  removed element shows read-only, reading "removed here". Where two fragments
  conflict, a row offers each fragment's value to write locally.
- An element this definition adds can be renamed, which re-keys its patch
  entry; an inherited element's key is read-only.
- A change to an open definition, or to a definition's file on disk,
  revalidates the open definitions composed from it. A definition that did not
  compose still records the sources it tried to read, so creating or fixing a
  missing or broken source re-checks it. Revealing a problem focuses its field.

## Scenes

Scene composition (`docs/editor/scene-composition.md`) uses the same operator.

- A version-1 instance patch applies through `ApplyCompositionPatch` with no
  schema and `CompositionNull::IsAValue`: objects merge, anything else is
  written whole, and `null` is written.
- A version-2 instance patch, and a prefab's own `patch` over what it extends
  and includes, compose with `CompositionNull::Removes` and an entity schema
  built from each component's `IComponentSerializer::CompositionSchema`, so a
  component's keyed lists merge by key.
