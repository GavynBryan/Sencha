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

### P1 — Data-asset references authorable; `CharacterMovement` scene form

### P2 — Derived components as an invariant of the mutation primitive

### P3 — Recipe to prefab on the wire

### P4 — Camera in the prefab; residual deletions

Each phase's detail lives in the approved execution plan; this document records
what landed and why. Phases update their own status line as they complete.

## 4. Rules this roadmap establishes

1. An interim shape records successor, trigger, and owner.
2. A component's authored form is part of shipping the component, not a
   follow-up.
3. Scratch components a system needs are owed by the component that triggers the
   system, not ensured by each caller.
