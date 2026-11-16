# Composition

One operator composes authored values: a composed base, then fragments, then a
local patch. Data definitions compose through it when they load and when Kyusu
lays them out, and scene instance patches apply through it. Nothing composes at
run time: systems see one ordinary compiled value.

The operator is `ComposeLayers` in `core/composition/Composition.h`, a template
over a small value-access trait (`CompositionValueTraits`). `JsonComposition.h`
implements the trait for `JsonValue`; the editor's `Json5Composition.h` does it
for `Json5Value`, comments included. `ApplyCompositionPatch` applies one patch
as a local layer.

## Rules

```text
composed = Local( Includes( composed(base) ) )
```

- **Absent inherits, present overrides.** A member the patch leaves out keeps
  the inherited value. A member it states overrides, even when equal to the
  inherited value.
- **`null` removes,** under `CompositionNull::Removes`. Removing a required
  member is `cannot_clear_required`. Under `CompositionNull::IsAValue` (scene
  instance patches, version 1) `null` is written as a value.
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
  cycle, a chain deeper than 16 definitions, a source that does not exist or
  does not compose, and a source of another subtype.
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
definition's source. Each cache entry records the definitions it composed from,
and `DataAssetCache::ComposedFrom` answers which resident definitions a path
feeds. The hot reloader consults it for every changed file, resident or not, so
editing an abstract base reloads its concrete children. A child that fails to
recompose keeps its last good value and reports at its authored location.

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
- A change to an open definition revalidates the open definitions composed
  from it. Revealing a problem focuses its field.

## Scene instance patches

A version-1 instance patch applies through `ApplyCompositionPatch` with no
schema and `CompositionNull::IsAValue`: objects merge, anything else is written
whole, and `null` is written. Keyed scene patches are not part of this format.
