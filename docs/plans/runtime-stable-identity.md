# Runtime Stable Identity

Status: shipped (2026-08-06), first slice, hardened after review. This document
records the identity model, the decisions behind it, and the seams left for the
save system, prefabs, and networking. It answers the open question recorded in
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
and the archive layer truncates integrals to u32. The codec is strict: anything
that is not a nonzero 16-digit lowercase hex value fails the parse.

**Bit 63 is the runtime namespace.** Editor mints keep it clear. The runtime
allocator, `MintRuntimePersistentId` (`world/identity/PersistentIdMint.h`), owns
the high half and can never collide with authored content. It mints from a
counter held as World state (`PersistentIdMint`), never from the wall clock or
addresses, and only where the simulation is authoritative; a client is refused
with an invalid id. `EnsurePersistentId` returns an entity's id, minting one on
the authority when it has none. The snapshot writer never sends a runtime id as
an authored one, since no client imported a copy to recognise. The reservation
is enforced, not just documented: `IsAuthoredPersistentEntityId` gates editor
load validation, the world cook, and the package importer, so content holding a
reserved id is rejected rather than cooked or imported.

**Reading a file never mints.** Minting happens when the editor creates or
adopts an entity; a load only validates. A document whose entities lack
identity, share an id, or hold a reserved-namespace id fails to load instead of
being repaired. Repair-on-load was rejected because it rewrites the user's file
behind them and lets a cook bake ids the source never recorded — the next cook
would then produce different ones, which is precisely the instability this
scheme exists to remove. Content predating identity was converted once rather
than carrying a permanent migration path (Sencha is pre-release; replaced
mechanisms are removed, not deprecated).

**One editor boundary owns identity.** `EditorScene` mints, indexes, and
releases ids; adoption (`TrackEntity`) is where identity is established, so no
route into a document can leave an entity unidentified regardless of how it was
created. The component is non-removable and non-addable through the inspector,
and `SetComponent<PersistentIdComponent>` is a compile error.

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
- Editor minting: `EditorScene::MintPersistentId` and `EnsurePersistentId`
  (kyusu), over a scene-owned id index so minting and duplicate detection are
  lookups rather than scans of the entity list (a bulk paste stays linear).
  Creation mints; adoption (`TrackEntity`) applies the liveness rule;
  `DestroyEntity` releases the id, which is what makes undo of a delete
  identity-preserving.
- References: `PersistableRef` (`world/identity/PersistableRef.h`) holds the
  target's id and the handle it last resolved to; a scene stores the id, and
  the handle is found again through the index once the target is resident.
  The importer makes every member's entity before it reads any component and
  hands the codec the package's members by the id they were authored under
  (`PackageMember`, `PackageEntity::MemberId`), so a reference between members
  of a spawn, whose ids the spawn strips, finds that spawn's member.
- Editor load validation: `EditorScene::ValidateIdentities` fails the load when
  a document's entities lack identity, share an id, or hold a reserved-namespace
  id. It only inspects; nothing on the load path mints.
- Cook validation: `ValidatePersistentIds` in kyusu `WorldCook.cpp` fails the
  world cook on a duplicate id within or across zones (the hand-copied
  `.level` file case) and on a reserved-namespace id. The passthrough cook
  carries ids verbatim.
- Cook input discipline: `CookSession::Start` saves a dirty level document
  before cooking, matching what world mode already did, so authoring edits reach
  the file before they reach an artifact. The cook needs no identity-repair
  guard of its own: a document that would have needed repair does not load.
- Package identity agreement: the importer rejects a package whose
  `ZonePackageEntity::PersistentId` metadata disagrees with the imported
  `persistent_id` component. The two drive different decisions (suppression
  before the row exists, index registration after), so disagreement would
  suppress under one identity and resolve under another.
- Zone state memory: `zone/ZoneStateStore.h`, a World resource added by
  `RuntimeWorld`. Import records the authored id set and suppresses
  recorded-destroyed entities (`ZonePackageImporter`, using per-entity
  identity lifted into `ZonePackageEntity` at package build); capture diffs
  live ids against the authored set while entities are still alive, at detach
  inside `FinalizeResidencyProcessing` and on demand for a save
  (`LoadedLevel::CaptureState`). Both go through `CaptureZoneState`
  (`zone/ZoneStateMemory.h`). Destroyed-is-remembered is computed as authored
  minus live, so repeated residencies accumulate without a union step. The
  authored baseline is released at detach because every import restates it,
  so retained memory tracks deviation rather than zones visited; a capture
  with no baseline is ignored rather than treated as "everything survived". A
  machine that is not the simulation authority neither imports nor counts as
  authored a member carrying an authority-materialized component
  (`SENCHA_AUTHORITY_MATERIALIZED`, `docs/core-systems-map.md`), so its detach
  never records that member destroyed. A
  suppressed parent leaves its child unparented, matching how destruction
  orphans rather than cascades. Every record is keyed by level and zone
  (`ZoneStateScope`, from `ZoneStateStore::Scope`): `LoadedLevel` makes the
  scope when it starts an import, `RuntimeZoneRecord::StateScope` keeps it,
  and the detach capture reads it from the record, so it cannot capture under
  the wrong level. A scene level's one zone is `kSceneLevelZone`; a world's
  zones are keyed by manifest id and its world scene by `kWorldSceneZone`,
  captured when the world unloads. The same scene file loaded as a map and as
  a zone of a world is two levels with separate records. The world cook and
  the manifest reader refuse a zone with either reserved id.
- Changed entities: when the game keeps level state
  (`SavedGames::KeepLevelState`, which installs `ZoneStateSerialization`), the
  importer records each authored entity's baseline beside its id: the cooked
  records of its `Saved` components (a component cooked as bytes has no record,
  is not diffed, and is reported once per zone and type). Capture diffs each
  live authored entity against its baseline through the entity patch
  (`world/serialization/EntityPatch.h`) and records a non-empty patch with the
  entity's lifetime; an empty one is forgotten. A `Saved` component the entity
  gained is diffed against its default record. At import the importer copies a
  patched entity's package record, composes the patch over it and builds the
  entity once at its final value. What a retune set aside is a warning and the
  scene decides; a member that no longer fits follows content, is logged as an
  error naming the entity and the component, and leaves the store; a member
  naming a component this build lacks is carried untouched. Only the authority
  records or applies patches: a client imports its zone as authored. Without
  `ZoneStateSerialization` a zone keeps destroyed sets only.

### Zone state lifetime

The zone record alone cannot tell a killed enemy from a taken pickup: both are
authored entities that are gone. `ZoneStateLifetime`, authored on an entity,
says when the zone forgets it was destroyed or changed:

| `forgotten` | Destroyed or changed, it stays so | Use |
|---|---|---|
| `never` (no component) | for good, and in saves | pickups, doors, switches, bosses |
| `at_restore` | until a save is restored or the level restarts | enemies that return when the player rests |
| `at_detach` | until its zone unloads | enemies that return when the player comes back |

A child takes its nearest ancestor's lifetime, and an entity of a placed
prefab without one of its own takes the one authored elsewhere in the same
placement, so marking a prefab's root marks the whole enemy. The importer
works each entity's lifetime out when it imports the zone and the store keeps
it beside the authored id, because a destroyed entity is not there to ask.
Only what is never forgotten is durable: `ZoneStateStore::ExportDurable` is
what a save keeps, every level's zones as the encoded lists a save file holds
(`docs/gameplay/saves.md`, "The file"), and `Install` replaces the store with
them. A zone that is not loaded is held as that encoded blob, parsed again when
it imports, unless it holds `at_restore` state, which the blob does not carry;
a write copies each blob's pointer, so a zone untouched since the last write is
the same blob in both. `ForgetLevel` forgets one level.

Worlds without the resources (editor documents, minimal fixtures) skip all of
it; the import and detach paths check for the store and for component
registration before doing any work. Nothing here runs per frame.

## 4. Deferred, with owners

- **Save serialization of the store** shipped with saves
  (`docs/gameplay/saves.md`): the durable part of every zone's record is
  written to the save and installed when it is restored.
- **Changed-field capture and off-thread overlay serialization** stay with
  Track C item 5 (stateful detach). The capture point it needs is the one this
  slice added in `FinalizeResidencyProcessing`.
- **Cross-zone residency transfer** ("the NPC is in another zone right now")
  rides `MoveEntityToPartition` plus a residency record in the store. Trigger:
  the first design need that moves an authored entity between zones at
  runtime.
- **Runtime mint allocator**: shipped (`MintRuntimePersistentId`, above). A
  hand-edited cooked scene can no longer carry a reserved id into the runtime
  index: the importer refuses the package. The counter is not saved, because
  nothing a save keeps names a runtime id yet; saving what was created at run
  time (`docs/deferred.md`) saves it with the first such entity.
- **Prefabs** (Track D item 1): a placed instance mints its own
  `PersistentEntityId` like any authored entity; the template it instantiates
  is an `AssetId`. Nothing in this slice constrains prefab internals.
- **`NetEntityId`** stays with the networking track: session-scoped, minted by
  the server for dynamic entities; authored entities need no spawn messages
  because both ends share the cook-stamped ids.

## 5. Compatibility

Authored scenes must carry `persistent_id` on every entity; an editor document
without it does not load. Content authored before identity was converted in a
one-shot pass rather than migrated at runtime, so no legacy path remains. The
runtime is unaffected by the editor's strictness: cooked scenes may contain
entities with no identity at all (cell meshes, lightmap entities), and the
runtime treats absence as "not identified" rather than as an error.

The binary scene golden moved once because the format writes a chunk per
registered serializer; the loader reads chunks by id, so old payloads still
load. No cooked format version bump. Gameplay-tag-style registration-order ids
are never serialized; `PersistentEntityId` values are stable across machines,
cooks, and sessions by construction.
