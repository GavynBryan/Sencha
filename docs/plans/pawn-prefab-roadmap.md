# The Pawn Prefab Roadmap

Status: executing (started 2026-08-29). Phase status is recorded per phase below
and updated as each lands.

Audience: anyone touching component registration, scene serialization, derived
component provisioning, net spawn, or the template game's archetypes.

The problem in one sentence:

> A player pawn is described in four places that nothing forces to agree, and
> the one of them a designer can edit describes almost none of it.

---

## 0. What the pawn actually is today

`template/assets/prefabs/player_pawn.sscene` carries a name and a transform.
Everything that makes a pawn a pawn is built in code, twice:

| Where | What it declares |
| --- | --- |
| `BuildPawnBody` (authority) | ~18 components, added if missing |
| the client spawn recipe | a second, smaller list for the same archetype |
| the prefab | name, transform |
| `FollowLocalControl` | calls `BuildPawnBody` again on replicated subjects |

The lists are kept in agreement by hand. When they disagree the failure is
silent: an entity missing one scratch component simply stops matching a query
and stops moving.

## 1. Why it got that way

Five root causes, each verified in the tree before this roadmap was written.

**RC1 — authored forms exist but are unwired.** Complete, correct name-based
scene serializers for `GameplayTagContainer` and `AttributeSet` shipped with
zero call sites: there was no entry point for a bespoke serializer in the
registrar every feature registers through, so the two free functions that
install them were never called from anywhere. `CharacterController` and
`AimFacing` lacked only a `TypeSchema`.

**RC2 — data references were unauthorable.** No `SceneFieldCodec` reaches a
data asset, so `CharacterMovement`'s profile handle and locomotion mode had no
scene form and the component could not be authored at all — despite every
ingredient existing (a path/handle cache, a mode registry with name lookup, the
mesh codec and component-traits patterns to copy).

**RC3 — nothing provisions derived components.** Only `WorldTransform` is
modeled as owed, by a hand-written storage-traits specialization. The eleven
per-tick scratch components a mover needs are ensured by hand at each spawn
site, so a new spawn path starts life one forgotten component away from a
frozen entity.

**RC4 — the client spawn path was a confessed placeholder.** `NetSpawnRecipe`'s
own header says the recipe id becomes an `AssetId` and the registry goes away;
`docs/plans/networking.md` §6.1 reserved the encoding. Both were written before
the scene pipeline could instantiate synchronously. It can now.

**RC5 — the interim shape outlived its trigger (the meta-cause).** RC4's
successor was named, and its trigger — a synchronous prefab instantiation path —
fired when the unified scene/prefab work landed. Nothing scheduled the
substitution, because the note named a successor and a trigger but no owner.

**The lesson, and the rule that comes out of it:** an interim shape records
three things or it is not recorded — the successor, the trigger that retires it,
and the owner who performs the substitution when the trigger fires. A fired
trigger with no owner is how a placeholder becomes architecture.

## 2. Where this ends up

```
authored component
  -> registrar / schema
  -> serialization, replication, dependency policy
  -> derived dependency closure
  -> final archetype signature
  -> ONE initialization path
```

Net spawn becomes: stable prefab identity -> resolve and validate ->
instantiate the complete local group -> bind the root's net identity -> apply
the snapshot (loud on any disagreement) -> commit. No recipes, no
`BuildPawnBody`, no ensure loops, no "probably built" sentinels, no silent
skips.

## 3. Phases

### P0 — Wire the dead serializers; trivial schemas (landed)

- `ComponentRegistrar::AddSerializer` — the entry point RC1 was missing. Same
  serializer twice is idempotent; a different serializer claiming an
  already-claimed identity facet fails loudly. Added serializers join the
  module-retraction list like schema-driven ones.
- `GameplayTagContainer` and `AttributeSet` serializers are registered beside
  their `Add<T>()` in the ability kit, so runtime, editor, and cook gain them
  from one edit.
- `TypeSchema` for `CharacterController` and `AimFacing`; `LookOrientation`
  gains a scene chunk id. Authoring `yaw` is the sanctioned way to state a
  start facing (see `AimFacing`'s own contract).
- Hygiene: the audio clip field declares its asset binding (and the editor
  learns to read that binding); the template stops hand-writing the derived
  world transform; a stale comment claiming nothing attaches transform history
  is corrected.

### P1 — Data-asset references authorable; the movement scene form (landed)

- `AssetSystem::LoadLease` / `GetPathForLease`: the synchronous load and the
  path lookup for a kind the front door cannot name, structured data included.
  A lease rather than a raw handle, because the caller is usually not the final
  owner. The template's hand-rolled stage/commit is gone.
- **`CharacterMovement` split.** The engine forbids a replicated component from
  declaring lifecycle hooks (snapshot apply overwrites bytes in place and fires
  none), so a component that owns an asset handle cannot be replicated. The
  profile — already `LocalOnly`, so never on the wire — moved to
  `MovementTuningSource`, which owns its reference through `ComponentTraits`.
  `CharacterMovement` keeps the mode and stays replicated and predicted.
- Both persist by name through hand-written serializers: the profile as an
  asset reference, the mode as its registered name. So does `AbilitySet`.
- `SceneAssetRef`: how a scene names an asset, extracted from the field codecs
  so a hand-written serializer resolves references the same way — including the
  `{id, path}` form the cook stamps, which is the shape a bespoke serializer is
  most likely to get wrong. (It did, once.)
- A registry that will hold a loaded scene now carries the vocabulary content
  names resolve against — tags, attributes, abilities, locomotion modes — in
  both the runtime and the editor document.
- Content: the pawn prefab carries a controller, its movement tuning and mode,
  its look orientation and aim facing, its tags, attributes, and ability set.
  The turret carries its aim.

**Deferred out of P1, with the reason:**

- *The pawn's mesh stays with the avatar data asset.* A headless cook has no
  mesh cache (a `StaticMeshCache` holds GPU resources), so a scene naming a mesh
  cannot round-trip through one — it refuses on save rather than dropping the
  reference. Moving the mesh into the prefab needs the cook to run in a
  composition that can hold one. Folded into P4, where the avatar chain is
  deleted anyway.
- *An asset picker for data-asset fields in the inspector.* Nothing has a
  schema-driven data-asset field yet; the two components that name one persist
  through hand-written serializers, which expose no inspector fields at all.
  Trigger: a designer needing to pick a profile in the inspector.

### P2 — Derived components as an invariant of the mutation primitive (landed)

`ComponentTraits<T>::DerivedComponents` declares the set T cannot work without,
beside T. The invariant lives in the typed add itself rather than in a layer
above the World: `AddComponent<T>` provisions the owed set add-if-missing
through the same typed path, which applies each provisioned component's own set
in turn. The closure is therefore transitive by construction, duplicates
collapse, and a cycle terminates on what is already there (and is refused at
Seal, where the whole graph is visible). No ordering: an owed component's OnAdd
may not assume a sibling.

Every path inherits it because they all end in the typed add — content loads,
code, and a snapshot creating a component on a client. `InitializeComponent`
carries the same obligation, so a row somebody else built is not a way around
it.

The batch import keeps its own route for one reason: the typed add would
migrate the row once per owed component, and a pawn owes eleven. The sealed
schema mirrors each component's closure by id, `BuildEntitySignature` ORs it in,
and the columns the package did not carry are written at their initializers with
their hooks firing. A test proves the two routes build the same entity, and
another proves the import still costs one row.

`CharacterMovement` owes the eleven columns the movement tick reads and writes.
The template stopped ensuring them: `BuildPawnBody` is down to the controller,
the tuning source, the movement component, and the things a pawn is rather than
the things it needs. On the prefab path only the avatar mesh is left, and
`FollowLocalControl`'s reassembly is marked for deletion in P3.

**Deferred to P3, with the reason:** the observer pawn. It replaces the
procedural pawn, which is not deleted until `BuildPawnBody` is — and a body that
flies with collision needs a locomotion arrangement of its own, since free
locomotion projects the wish direction onto the ground plane by design. That is
a movement change, not an ECS one, and it belongs in the commit that removes
what it replaces.

### P3 — Recipe to prefab on the wire (landed)

`NetSpawnPrefab{ AssetId Scene }` replaces `NetSpawnRecipe{ u16 }`, and the
overrides are the snapshot itself. `NetSpawnRecipes` — the registry where each
game re-described its own entities in code, on both ends — is deleted, along
with `BuildPawnBody`, `BuildTurretBody`, and the client-side reassembly in
`FollowLocalControl`.

**The gate, and what it actually found.** `AssetId` is minted from the asset's
virtual path, so it does not depend on the order content was seen in; a test
pins that. What the gate did not initially ask was whether scenes *have* ids —
and they did not. The cook minted ids for what a scene references and never for
the scene itself, because a scene is nobody's dependency. So the cook now mints
one for the artifact it publishes. Without that, every prefab on the wire would
have been the invalid id, and every client body would have deferred forever
while the tests still passed.

**How the applier changed.** The prefab is read out of the snapshot's own bytes
while planning, and `Prepare` decides then whether it can be built. Not ready
means deferred — read, dropped, unacknowledged, described again — exactly as an
unresolved authored key already was. Never built bare.

Two things the write had to learn, both because a prefab arrives already
holding much of what the snapshot is about to say:

- Whether a component is present is decided at the write for a spawn, not at
  the plan. The plan could not know: the entity did not exist yet.
- The write merges rather than overwrites. The staged image was seeded from
  type defaults, so stamping it whole would erase everything the prefab set
  that the wire does not carry — an aim limit, a tuning handle, anything
  local-only. The decoder now reports which fields it carried, and only those
  are written.

A replicated component the snapshot carries that the prefab lacks is imported
anyway — the wire is authoritative — and counted and named, because the two
ends disagreeing about what an entity is must not be absorbed silently.

`FieldScalar::UInt64` is appended rather than inserted: the value is hashed
into every component's schema fingerprint. Even so, StrongId leaves moved from
`Unsupported` to `UInt64`, so every cooked scene in an existing working tree
needs one recook — reported loudly by the fingerprint gate, which is what it is
for.

The observer pawn replaces the procedural one: a capsule that collides and
flies, built from engine behaviour only, steered from the full aim basis with
its vertical channel forced through `MotionAxisOverride` — which is also what
keeps gravity off it. Loud when it engages.

**Recorded gap.** The cooked-cache key does not include the component schema, so
a schema change leaves stale artifacts until something recooks. The failure is
loud and names the component, so this is a convenience gap rather than a
correctness one; the fix is to fold the schema identity of the components a
document contains into its cook fingerprint.

### P4 — Camera in the prefab; residual deletions

Each phase's detail lives in the approved execution plan; this document records
what landed and why. Phases update their own status line as they complete.

## 4. Rules this roadmap establishes

1. An interim shape records successor, trigger, and owner.
2. A component's authored form is part of shipping the component, not a
   follow-up.
3. Scratch components a system needs are owed by the component that triggers the
   system, not ensured by each caller.
