# Mesh overrides

A mesh override hides material slots of an entity's mesh, or substitutes their
materials, without writing the mesh component. Several writers can override one
entity at once (an equipped helmet hiding the hair, an effect tinting the skin),
each through its own contribution, and taking a contribution back restores
exactly what was authored. What a game shows and when is the game's policy; this
is the mechanism it uses (`render/MeshOverrides.h`).

## Contributions

`SetMeshOverride(world, entity, contribution)` adds or replaces the contribution
of `contribution.Owner`, a `PresentationOwner` (`presentation/PresentationOwner.h`):
the writer, numbered per World by `RegisterPresentationWriter`, and a
`LeaseOwner` the writer chooses. Owners compare whole, so two writers using the
same key never collide. `ClearMeshOverride` takes one back.

A contribution names slots with `MeshSlotSelector`:

- `DrawnMaterial`: every slot whose authored material is the given one. The
  cook makes one section per material for skinned meshes and one per glTF
  primitive (one per material, as exporters write them) for static meshes, so
  the material a section draws is how an artist's region is named. It survives
  re-export and renames, since asset ids are stable. Two regions that share one
  material cannot be told apart.
- `Slot`: one slot by index, for content with no distinct material. A slot past
  the authored set names nothing and is reported (`SlotOutOfRange`).

A contribution `Hide`s slots and `Substitute`s materials into slots, and keeps
every material it substitutes resident while it is held.

## Composition

An entity's contributions are composed when one is set or cleared, and when the
authored material set changes under them (`MeshOverrideSystem`, every frame on a
presenting host, over entities with overrides only). Never per draw.

- Hidden slots are the union of every contribution's hides.
- A slot two contributions substitute goes to the higher `Order`. At equal
  `Order` with different materials, the outcome reports a tie and the owner first
  by `PresentationOwnerPrecedes` wins, so every machine settles it alike.
- The result is the authored slot list with substitutions applied, interned in
  the `MaterialSetCache` (deduplicated, so equal looks share one set) and owned by
  the entity's `MeshOverrides` component.

`MeshOverrides` is added at an entity's first contribution and kept, so
contributing again never moves the entity between archetypes. Contributions
themselves live in the World's `MeshOverrideContributions` resource, not in
rows, so the component is 20 bytes. Removing the component (the entity's
destruction, a zone unloading) releases the composed set and every substitute
its contributions held.

## Extraction

Static, skinned and shadow-caster extraction call `ApplyMeshOverrides` per mesh:
the section mask loses every section whose slot (`ResolveSectionSlot`, the same
fallback `ResolveSectionMaterial` uses) is hidden, and the composed set replaces
the authored one. They read `MeshOverrides` through `ChunkView::TryRead`, a
per-chunk optional column, so an archetype without overrides pays one column
lookup per chunk and nothing per entity, and no query is duplicated.

While the authored set differs from the set the overrides were composed against,
`ApplyMeshOverrides` returns the authored state unchanged, so a stale
composition never hides or substitutes the wrong slot. A shadow cast record
carries the composed set, so a changed look invalidates its cached shadow.

Skinned meshes cast no realtime shadows (see [shadows.md](shadows.md)).
