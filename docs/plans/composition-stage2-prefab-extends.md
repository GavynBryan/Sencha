# Composition Stage 2: prefabs that extend prefabs and include fragments

Stage 2 of `docs/plans/definition-composition-inventory-equipment.md` (§G.2,
rules R1–R6 in §B.1, format in §E.1, walkthrough D.2). It reuses the Stage 1
operator (`core/composition/Composition.h`) for scenes. It needs no item code,
and its gate uses none.

**Status on main.** The format, resolver, projection and harvest, cook and
recook are ported (S2.1–S2.5 and the gate's cook half), with the Stage 2–4
audit's composition fixes: a fragment that extends is refused, removals are
checked as a whole list, a lost inherited parent is `dangling_parent`, a
cleared member is harvested as `null`, Kyusu opens a version-1 document as
version 2, and a placement renames its members' references. The Kyusu sheets
(S2.6) are deferred (`docs/deferred.md`, "Kyusu authoring for prefab
extends"). The ABI change is recorded under v48, and the cooked `.smap`
format is unchanged by this stage.

## What Stage 2 delivers

- **`extends`.** A prefab extends another and adopts its entities and their
  ids. There is no wrapper entity.
- **`includes`.** A prefab includes fragments. Each fragment is merged onto
  the root entity, or onto the entity its `onto` names.
- **Patches.** A prefab patches what it inherits. Keyed lists merge by key,
  and `null` removes an element.
- **Removal.** A prefab removes inherited components. Removing a component
  another one owes is a compose-time error (R4).
- **Abstract.** `abstract` prefabs are never cooked or placed (R5).
- **Format.** The scene format goes to version 2. Version-1 files read exactly
  as before (D2).
- **Recooking.** Dependents recook when a prefab they compose from is saved.
- **Kyusu.**
  - It shows inherited values with their provenance.
  - It writes only what changes.
  - It offers revert, restore and take-value verbs.
  - It has envelope rows for `extends`, `includes` and `abstract`.

## Invariants

1. **One resolver.** Composition stays in `ResolveSceneComposition`, so the
   editor projection and the cook cannot disagree. The runtime never composes;
   it loads the flattened `.smap`.
2. **One operator.** Every component merge goes through
   `ApplyCompositionPatch` / `ComposeLayers`. There is no scene-specific merge.
3. **Identity is recorded, never derived (D3).** Inherited entities keep the
   base's ids, and a local entity may not reuse one.
4. **Composition knows nothing about items (R6).**
5. **Version-1 scenes compose exactly as before.** That means whole-array
   replacement, `null` as a value, and no schemas.

## Design

### Components declare how their arrays merge

- `IComponentSerializer` gains
  `virtual const DataFieldSchema* CompositionSchema() const { return nullptr; }`.
  - It returns a record schema describing the component's scene form, enough
    for the operator to merge keyed arrays.
  - With no schema, the version-1 rules hold: objects merge member by member,
    and everything else replaces whole. Under R2 that is also right for
    unkeyed arrays.
  - It is tooling metadata, like `IsRemovable`. Game modules may declare it for
    their own serializers.
- Three engine forms declare keys:
  - `AbilityGrants.grants` by `key`;
  - `GameplayTags.tags` by `name`;
  - `Attributes.attributes` by `name`.
- For a whole entity, the resolver builds one record schema whose members are
  each component's schema under its `JsonKey`, so one `ComposeLayers` call
  composes all of an entity's components.
- **ABI.** `IComponentSerializer` is fingerprinted. This joins the
  unreleased v29 bump and is recorded in `module-abi-history.md`.

### Format version 2

```json5
{ "id": "…", "format_version": 2,
  "extends": "asset://prefabs/actors/humanoid.sscene",
  "includes": [ { "fragment": "asset://prefabs/fragments/alertable.sscene" },
                { "fragment": "asset://prefabs/fragments/torch.sscene", "onto": "11c04b1a77e0c002" } ],
  "abstract": false,
  "patch":  { "11c04b1a77e0a001": { "Attributes": { "attributes": { "Health": { "base": 200 } } } } },
  "remove": { "11c04b1a77e0a001": [ "LookOrientation" ] },
  "entities": [ /* new entities; a parent may be an inherited id */ ],
  "instances": [ /* as today; their patches merge by key in v2 */ ] }
```

- **Reading.**
  - `format_version` 1 or 2 is read. Root `extends`, `includes`, `abstract`,
    `patch` and `remove` are refused in a version-1 file. Today they would
    silently land in `UnknownRoot`.
  - In version 2, an instance's `patch` composes with schemas and `null`
    removes. In version 1 it keeps whole replacement and `null` as a value.
- **Writing.** Kyusu always writes version 2.
  - Harvesting recomputes every override from live state and baseline, so a
    version-1 file saved in Kyusu composes to the same entities.
  - Its array overrides become keyed maps where the component declares a key.
  - There is a fixture for exactly this.
- **Validation.** A local entity's `parent` may name an inherited id, which is
  checked at resolve time because only the resolver knows the base's ids. A
  local id that collides with an inherited one is an error.

### A fragment is one entity

- A fragment is an `abstract` `.sscene` with exactly one entity. Its
  components merge onto the target as an `include` layer.
- A fragment with more entities is refused (`fragment_shape`).
- Contributing entities, such as a torch child, is what `extends` and
  instances are for. Widening this later is additive.

### Composing a document with `extends` and `includes`

For document *D*:

1. **Resolve the base** with the same resolver. Its entities, with ids, paths,
   parents and expanded instances, become D's inherited entities, with no
   synthetic root. A cycle or an unresolvable base fails, as instance sources
   do today. The depth limit is shared with instance nesting.
2. **Apply fragments, then the local patch.** For each inherited entity *e*,
   `ComposeLayers` runs with:
   - the base layer: *e*'s composed components;
   - include layers: the fragments targeting *e*, in order;
   - the local layer: `patch[e]`.

   The policy is the entity schema above, `CompositionNull::Removes`, with
   provenance. This gives:
   - R1: `include_conflict` when two fragments disagree, unless the patch
     states the value;
   - R2 and R3: keyed merges, `null` removal, a present value overrides;
   - per-field provenance (base, include or local, with the asset).
3. **Apply `remove[e]`.** Removing a component another remaining component
   owes fails with `removes_owed`, naming both (R4). Removing a
   non-removable component fails with `not_removable`.
4. **Append local entities and instances.** They resolve as today.

Two more rules:
- Diagnostics carry the composing document and a path:
  `<entity id>.<Component>.<field>` for components, `$.includes[i]` and
  `$.extends` for the envelope.
- The result gains three per-entity fields:
  - `Inherited`: the base and fragments, before the local layer, which is the
    harvest baseline;
  - `Provenance`;
  - `RemovedComponents`.

**Component rules seam.** The resolver lives in `editor/common` and has no
World. A small `ISceneComponentRules` beside `ISceneSourceLookup` answers two
questions per JSON key: the component's composition schema, and which
components it owes (closure). The level editor answers from its serializer
registry and `World::DeclaredOwedComponents`. Tests answer from a table.

### The derived prefab in Kyusu

- **Projection.** Inherited entities project as live entities, much as
  instance members do. The difference is that they belong to the document's
  own root `patch`, not to an instance record.
  - Their baseline is `Inherited`.
  - The harvest writes:
    - a sparse root `patch`, with keyed arrays as maps and removed keyed
      elements as `null`;
    - a root `remove` for inherited components deleted in the editor.
  - Deleting an inherited entity is refused, with a message. Suppressing base
    entities needs a format surface the RFC does not define.
  - Adding entities and parenting them under inherited ones is allowed.
- **Keyed diff.** The harvest diff (`DiffValue`) learns keyed arrays when a
  schema declares them. Live elements diff against base elements by key; a
  key only in live is whole; a key only in base is `null`. The same diff
  serves instance overrides in version 2, so instances gain keyed overrides
  and tombstones too.
- **Sheets.** `ComponentPropertySource` gains, for inherited and instance
  entities, provenance in the row detail:
  - "inherited from `humanoid.sscene`", "from `alertable.sscene`", or
    "set here";
  - "Reset to Source" is renamed "Revert to inherited" for inherited entities
    and keeps its behaviour;
  - **Restore** is added for a removed inherited component, and erases it
    from `remove`;
  - **Take `<fragment>`'s value** is added on an include conflict.

  The envelope rows (Extends, Includes, Abstract) are a property source shown
  for the document, as data documents have.

### Cook and recook

- `CollectDocumentCookInput` refuses an `abstract` document: it is extended
  or included, never cooked.
- **Recording what was read.**
  - The cook records every source the resolver read as `AdditionalSources` in
    the cook index.
  - `SceneSourceCache` gains the list of documents it loaded.
  - Bases, fragments and instance sources are all recorded.
- **Recooking dependents.** When Kyusu saves a `.sscene`, the cooked documents
  whose recorded sources include it are recooked, on the cook lane, with the
  existing document cook.
- The derived `.smap` keeps the derived `.sscene`'s asset id through the
  same-stem rule, so `NetSpawnPrefab` and spawning are unchanged.
- **Prefab placement.** The prefab explorer does not offer abstract sources,
  and placing one is refused.

## Steps

Each step lands test first and is committed on its own.

| Step | What | Tests |
|---|---|---|
| **S2.1 Composition schemas** | `IComponentSerializer::CompositionSchema`; keys for AbilityGrants, GameplayTags, Attributes; ABI note | Each schema merges its form by key through the operator; `null` removes a grant |
| **S2.2 Format v2** | Parse/write `extends`, `includes`, `abstract`, `patch`, `remove`; v2 instance patches merge by key; v1 unchanged | Round trips; v1 fixtures byte-identical in meaning; v2-only members refused in v1 |
| **S2.3 Resolver** | Extends, fragments with `onto`, patch, remove; R1–R5 diagnostics; provenance; `ISceneComponentRules` | Table tests: adopt ids, keyed patch, null removal, include conflict, removes_owed, not_removable, fragment shape, cycle, id collision, inherited parent |
| **S2.4 Projection + harvest** | Inherited entities projected; keyed diff with tombstones; root patch/remove written | Edit inherited field → sparse patch; remove a grant → `null`; base change reaches child; undo/redo; revert; v1 file saved as v2 composes the same |
| **S2.5 Cook** | Abstract refused; sources recorded; recook dependents on save | Cooked derived `.smap` carries composed values; editing the base and saving recooks it |
| **S2.6 Sheets** | Provenance, verbs, envelope rows | Rows per situation; restore; take value |
| **S2.7 Gate** | humanoid → guard (includes alertable, combat_training) → elite_guard | Cooks, spawns through `SceneSpawnService`, inspects with provenance; no item code |

## Compatibility

- **Version-1 scenes.** Read and compose unchanged. Saved by Kyusu, they
  become version 2 and compose to the same entities.
- **Runtime.** `.smap` is unchanged (`kSmapVersion` 1).
- **ABI.** `IComponentSerializer` gains one virtual, within the v29 bump.

## Decisions taken in this plan

1. **The plan was written and implemented in one pass.** The owner asked for
   work to continue without stopping, so this plan was not reviewed before
   implementation. Every decision below is named so it can be reviewed
   afterwards.
2. **A fragment is one entity.**
3. **Deleting an inherited entity is refused.** There is no base-suppression
   format.
4. **Kyusu writes version 2 for every scene it saves.**

## Deviations from this plan, named during implementation

1. **A fragment goes only onto an inherited entity.** A fragment onto the
   prefab's own entity would be saved into that entity as its own value, so
   it is refused with `fragment_target` (`docs/deferred.md`).
2. **Deleting an inherited entity deletes only what the document authored
   beneath it.** The inherited entity stays, and reparenting it is refused.
   The plan called this "refused"; the delete command now leaves the
   inherited entity and its inherited children, and removes the document's
   own entities beneath it.
3. **There is no depth limit beyond cycle detection.** The plan said the
   limit is shared with instance nesting, but instance nesting has none. The
   resolver refuses cycles through `extends`, `includes` and instances
   alike.
4. **An adopted base's placements are flattened.** Its instance members
   become plain entities of the derived prefab: they are not placement
   members, and the cook stamps no `scene_instance` on them.
5. **Composition sources are recorded by path only.** Size and modified time
   stay unknown, which the cook index allows, because what a source edit
   recooks is found by path.
6. **Recooking runs on the owner thread.** The plan said "on the cook lane",
   but collecting a cook's input goes through the asset system, which belongs
   to the owner thread. It is skipped while a cook runs.
7. **The prefab explorer still lists abstract prefabs; placing one is
   refused with the reason.** The explorer lists files without parsing them.
8. **"Take a fragment's value" is not a verb in the scene sheet.** An include
   conflict blocks the cook and names both fragments. Editing the value on the
   entity writes it into the patch, which settles the conflict. The component
   inspector has no conflict-row surface.
9. **Two Stage 1 operator gaps closed here:**
   - include conflicts are now seen across a whole-record write and a member
     write;
   - an empty record records its own provenance.
10. **A single-scene save now propagates to dependents.** Before, only world
    saves did. Without this, editing a base prefab re-projected and recooked
    nothing.
11. **The inspector decides that a component was added here by whether the
    baseline has it.** It used to check whether the baseline's bytes were
    empty, which is always true for a tag component.
12. **The scene gate's spawn check is structural.** The cooked derived prefab
    has one root record and composed values, and carries the derived asset
    id. Spawning runs no new code, and the runtime spawn harness cannot
    register editor-cooked components.
