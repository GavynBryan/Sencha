# Runtime Stable Identity

Status: shipped (2026-08-06), first slice. This document records the identity
model, the decisions behind it, and the seams left for the save system,
prefabs, and networking. It answers the open question recorded in
`docs/action-adventure-core-runtime.md` ("what is the first stable entity
identity scheme for state overlays") and executes the Track D sketch in
`docs/plans/zone-membership-and-hardening.md` section 6.

## 1. The identity model

Four identities, one per lifetime:

| Identity | Type | Lifetime | Never |
| --- | --- | --- | --- |
| ECS handle | `EntityId { Index, Generation }` | one World, one session | persisted or sent on a wire |
| Persistent entity identity | `PersistentEntityId` (`StrongId`, u64) | authored content, saves, the wire | derived from registration or array order |
| Asset identity | `AssetId` (existing) | source tree and cooked cache | reused as entity identity |
| Network identity | reserved (`NetEntityId`, session-scoped) | one server session | persisted |

There is exactly one stable scene-entity identity scheme. The zone state
overlay, the save system (roadmap Track A item 8), and replication all join on
`PersistentEntityId`. A second scheme is an architectural defect.

## 2. Decisions

**Editor-minted, not cook-minted.** The editor mints the id when the entity is
authored (random nonzero u64, collision-checked in the document, following the
`DockId`/`LinkId` precedent) and it persists in the `.level.json`. Cook-minting
was rejected because a cook has no stable per-entity key to mint from: array
position shifts on every content edit, which is the disease, not the cure.
`AssetId` can cook-mint only because the asset path is a durable authored key.
The engine mints no random ids.

**Universal on authored entities.** Every entity an editor document creates
carries a `PersistentIdComponent`. Identity is not a feature flag; opt-in
identity would push a policy question into every placement workflow.
Cook-generated entities (cell meshes, lightmap entities) carry none.

**u64 with the AssetId text form.** Ids serialize as 16-digit lowercase hex
through a dedicated `SceneFieldCodec` because JSON numbers cannot hold 64 bits
and the archive layer truncates integrals to u32. `"0000000000000000"` is
accepted in authored scenes as unset so the editor can backfill.

**Bit 63 is the runtime namespace.** Editor mints keep it clear. A future
deterministic runtime allocator (persistent identity for dynamically spawned
entities, minted per save as a counter, never from wall clock or addresses)
owns the high half and can never collide with authored content.

**Identity follows liveness on restore.** A restored snapshot keeps its id
unless the id is unset or already held by a live entity in the document, in
which case a fresh id is minted. That one rule makes undo-of-delete and
cross-zone moves identity-preserving while duplicates and copies of live
sources mint fresh, with no per-command policy.

**The one-scheme rule supersedes the unmerged networking plan's detail.** The
networking plan drafted on `claude/sencha-networking-plan-x7fv5s` names the
retired 32-bit `SerializedEntityId` and cook-time minting in its section 3.4.
Its binding rule (one scheme) is adopted; its mechanism detail is superseded by
this document. `SerializedEntityId` was removed with no consumers.

## 3. Shipped mechanisms

- `PersistentEntityId` and text form: `core/identity/Id.h`.
- `PersistentIdComponent` plus codec and schema: `world/identity/PersistentIdComponent.h`,
  registered in `world/ComponentManifest.h` (appended last: the binary scene
  form writes one chunk per registered serializer, so manifest order is
  serialized state).
- `PersistentEntityIndex`: `world/identity/PersistentEntityIndex.h`. A World
  resource mapping id to live `EntityId`, maintained by the component's
  `ComponentTraits` hooks (the StaticMesh retain/release idiom), added by the
  `RuntimeWorld` constructor. First registration wins on collision; losers are
  counted and cannot evict the winner on destroy.
- Editor minting: `EditorScene::MintPersistentId`, `EnsurePersistentId`,
  `BackfillPersistentIds` (kyusu). Creation mints; restore applies the
  liveness rule; document load backfills legacy files in linear time and opens
  them dirty so mints reach disk.
- Cook validation: `ValidatePersistentIds` in kyusu `WorldCook.cpp` fails the
  world cook on a duplicate id within or across zones (the hand-copied
  `.level` file case). The passthrough cook carries ids verbatim.
- Zone state memory: `zone/ZoneStateStore.h`, a World resource added by
  `RuntimeWorld`. Import records the authored id set and suppresses
  recorded-destroyed entities (`ZonePackageImporter`, using per-entity
  identity lifted into `ZonePackageEntity` at package build); detach diffs
  live ids against the authored set inside `FinalizeResidencyProcessing`
  while entities are still alive. Destroyed-is-remembered is computed as
  authored minus live, so repeated residencies accumulate without a union
  step. A suppressed parent leaves its child unparented, matching how
  destruction orphans rather than cascades.

Worlds without the resources (editor documents, minimal fixtures) skip all of
it; the import and detach paths check for the store and for component
registration before doing any work. Nothing here runs per frame.

## 4. Deferred, with owners

- **Save serialization of the store** stays with roadmap Track A item 8: the
  overlay becomes durable by serializing `ZoneStateStore` records plus a
  global record. The in-session store is deliberately serialization-shaped
  (plain id sets keyed by zone).
- **Changed-field capture and off-thread overlay serialization** stay with
  Track C item 5 (stateful detach). The capture point it needs is the one this
  slice added in `FinalizeResidencyProcessing`.
- **Cross-zone residency transfer** ("the NPC is in another zone right now")
  rides `MoveEntityToPartition` plus a residency record in the store. Trigger:
  the first design need that moves an authored entity between zones at
  runtime.
- **Runtime mint allocator** for persistent dynamic spawns: bit 63 namespace,
  a per-save counter. Trigger: the save system needs a dropped item to
  persist.
- **Prefabs** (Track D item 1): a placed instance mints its own
  `PersistentEntityId` like any authored entity; the template it instantiates
  is an `AssetId`. Nothing in this slice constrains prefab internals.
- **`NetEntityId`** stays with the networking track: session-scoped, minted by
  the server for dynamic entities; authored entities need no spawn messages
  because both ends share the cook-stamped ids.

## 5. Compatibility

Authored and cooked scene JSON gain one optional component; files without it
load unchanged (the editor backfills, the runtime treats absence as no
identity). The binary scene golden moved once because the format writes a
chunk per registered serializer; the loader reads chunks by id, so old
payloads still load. No cooked format version bump. Gameplay-tag-style
registration-order ids are never serialized; `PersistentEntityId` values are
stable across machines, cooks, and sessions by construction.
