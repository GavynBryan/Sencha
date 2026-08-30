# The Pawn Prefab Roadmap

Status: landed 2026-08-30, with one item blocked and its blocker written down
(P4). Phase status is recorded per phase below.

Audience: anyone touching component registration, scene serialization, derived
component provisioning, net spawn, or the template game's archetypes.

The problem in one sentence:

> A player pawn is described in four places that nothing forces to agree, and
> the one of them a designer can edit describes almost none of it.

---

## 0. What the pawn was

`template/assets/prefabs/player_pawn.sscene` carried a name and a transform.
Everything that made a pawn a pawn was built in code, twice:

| Where | What it declares |
| --- | --- |
| `BuildPawnBody` (authority) | ~18 components, added if missing |
| the client spawn recipe | a second, smaller list for the same archetype |
| the prefab | name, transform |
| `FollowLocalControl` | calls `BuildPawnBody` again on replicated subjects |

The lists were kept in agreement by hand. When they disagreed the failure was
silent: an entity missing one scratch component simply stops matching a query
and stops moving.

The prefab now carries a controller, its movement tuning and mode, its aim and
the opt-in that turns the body to it, its tags, its speed, its ability set, and
the camera it is watched from. The per-tick scratch comes with the component
that needs it. `BuildPawnBody`, the recipe registry, and the client-side
reassembly are gone. What is still code: the avatar mesh, for the reason in P4.

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
  **Executed** — the trigger fired. `MovementTuningSource` describes its
  profile as an `AsDataAsset` field narrowed to `movement.profile`; the four
  components that persist as registry-resolved names draw their own inspector
  rows; and a game module declares its vocabulary into every authoring document
  through `Game::OnRegisterVocabulary`.

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

### P4 — Camera in the prefab; residual deletions (landed, one item blocked)

The pawn prefab places the camera it is watched from, as a child carrying
`CameraSeat`. The seat says which camera it is — `Primary` — and how it
watches, so a third-person game is the same pawn prefab with a different
camera child rather than a different code path. Possession reads the seat and
provisions the rig from it; who is watching is still a fact about the machine,
which is why the rig is not authored.

Not "the first child with a camera": a pawn may carry several (a scope, a
mirror, a cutscene angle), and picking by position means adding one silently
changes which the player looks through. A second primary is reported.

`CameraRigMode` and `CameraSeatRole` gained authored vocabularies, and
`CameraComponent`'s fields gained defaults, so a placed camera states only what
it means to change.

Deleted: the game-lifetime movement-profile lease, `ResolvePlayerMovementProfile`,
and the path constant. The prefab authors the profile; nothing threads it any
more.

**Blocked, and why: the avatar chain stays.** Deleting `player_avatar.sdata`
requires the mesh to move into the prefab, and a scene naming a mesh cannot
round-trip through a headless cook: a `StaticMeshCache` holds GPU buffers, so a
process composed without graphics has no cache, the load leaves the handle
invalid, and the save refuses because an invalid handle has no path to write.
The fixture that cooks the shipped prefabs runs headless in CI, so this is not
a matter of cooking them somewhere else.

The contract underneath it: the scene codec makes a process able to *load* an
asset in order to *name* one. A cook, a dependency scanner, and a headless
validator all want the name and none of them want the bytes. The fix is a
composition that resolves a path to a handle without reading the asset --
identity without residency -- at which point the mesh moves into the prefab and
the avatar data asset, `ResolvePlayerAvatar`, the threaded `Avatar` members, and
the temporary pass that dresses arriving bodies all go together.

## 4. Rules this roadmap establishes

1. An interim shape records successor, trigger, and owner.
2. A component's authored form is part of shipping the component, not a
   follow-up.
3. Scratch components a system needs are owed by the component that triggers the
   system, not ensured by each caller.
4. A gate proves the thing it is about, not a property near it. "Ids are minted
   deterministically" was true and did not answer "do scenes have ids" -- which
   was the question, and the answer was no.
5. A body that cannot be built is refused, never approximated. An entity with
   its state and no body has no error and no frame it happens on.
