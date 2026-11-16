# Composition

One operator composes authored values: a composed base, then fragments, then a
local patch. Scene instance patches apply through it.

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
handed it on.

## Scene instance patches

A version-1 instance patch applies through `ApplyCompositionPatch` with no
schema and `CompositionNull::IsAValue`: objects merge, anything else is written
whole, and `null` is written. Keyed scene patches are not part of this format.
