# Composition Stage 0: prerequisites and measurement

Status: **proposed for review** (2026-10-15). This is the implementation plan
for Stage 0 of `docs/plans/definition-composition-inventory-equipment.md`
(the RFC, revision 3, approved). It covers RFC defects A.4.1–A.4.4, A.4.9 and
A.4.10, plus the F.2 benchmarks. It depends on nothing else and nothing waits
on it except the D3 ruling.

**On main:** Stages 0 and 1 were ported from `claude/composition` onto a later
main as module ABI v48 and net protocol 9. ABI and protocol numbers below are
the branch's own.

Audience: the owner reviewing the plan, and whoever implements it.

Every fact below was read on main at 93476b54. "Read" means confirmed by
reading source; nothing here has been run yet.

## Scope

**In:**

| Step | What | RFC |
|---|---|---|
| S0.1 | Spawned prefabs that contain placements spawn as one group | A.4.1 |
| S0.2 | The ability pass revokes ended effect grants before granting new ones, and traces a grant that does not fit | A.4.3, A.4.4 |
| S0.3 | Grant entries are told apart on the wire by their content; a reloaded effect's grants are replaced, not duplicated; the pawn's first send is tested against one datagram | A.4.2, A.4.10 |
| S0.4 | Benchmarks: entry-shaped entities in the ECS and in replication; AbilityKit effect scans | F.2 |
| S0.5 | Documentation corrections the steps make necessary, plus the stale command-buffer doc | A.4.9 |

**Out (each in its own later stage):**
- composition (Stages 1–2);
- any inventory, item or equipment code;
- grant source, parameters, cost providers, reservations and `precedence`
  (Stage 3);
- baseline tag stacks (Stage 5);
- client materialization (Stage 4);
- **any optimisation the benchmarks suggest.** The benchmarks record numbers;
  the owner decides.

**Rules for every defect step:**
1. The regression test is written and run on unmodified main first.
2. It must fail for the stated reason; that failure output is recorded in the
   step's commit message.
3. Only then is the fix applied.

A test that passes on main means the claimed defect is wrong. The step then
stops and the finding is corrected in the RFC instead.

**Working tree.**
- A worktree `../Sencha-composition` on branch `claude/composition-stage0`,
  created with `--no-track` off a freshly fetched `origin/main`.
- One commit per step.
- A private `TMPDIR` for test runs, because fixture names collide across
  worktrees.

---

## S0.1 Spawned prefabs with placements spawn as one group (A.4.1)

**Invariant.** Every entity a spawn imports belongs to that spawn's group:
it carries `SceneInstance{spawned scene, runtime id}`.

**Owners:**
- `SceneSpawnService` and `NetPrefabSpawner` own the stamp;
- `BuildEntityPackageFromSmap` owns what a spawn keeps from cooked data.

**Evidence (read):**
- The document cook stamps `scene_instance {source, id}` on every member of a
  placement (`editor/level_editor/src/document/DocumentCookInput.cpp:354-362`).
- Both spawners build with `SmapPackageOptions{ .StripPersistentIdentity =
  true }` (`SceneSpawnService.cpp:155-156`, `NetPrefabSpawner.cpp:121-122`).
  That option drops `persistent_id` but not `scene_instance`
  (`SmapFormat.cpp:653-655`).
- Both then stamp with `(void)package->AddComponent(..., SceneInstance{...})`
  (`SceneSpawnService.cpp:158-163`, `NetPrefabSpawner.cpp:136-138`).
- `EntityBuildPackage::FindInsertTarget` refuses a duplicate component type
  (`EntityBuildPackage.h:182-197`), and the refusal is discarded.

**Consequences, all reached by a member that keeps the cooked id:**
- It is missing from `SceneSpawnService::Entities` (`:277`).
- It is not the composed root (`:308-315`). When the scene's only root is a
  placement root, which is today's "variant" pattern, `Root` is invalid and an
  `OwnedByRoot` spawn fails or asserts (`:318-335`).
- It is not destroyed by `DestroySceneInstance` (`:332`, `:399`), nor by
  `NetPrefabSpawner::Despawn` (`:176-184`).
- `templates/arena/src/PawnSpawn.cpp:82-96` replicates the root's
  `SceneInstance.Source` as the prefab to spawn on peers. A nested root would
  name the inner prefab.

**Nothing reads the cooked placement id at runtime.**
- Hooks only index it (`SceneInstance.cpp:10-26`).
- No engine, app or template code reads a non-runtime id.
- The editor works from document records.
- Dropping it from spawns loses nothing anyone uses.

### Tests first

In `test/runtime/SceneSpawnServiceTests.cpp`, using the existing `SpawnHarness`
and `TempSmapScene` (`test/runtime/SmapSceneFixture.h`). A cooked member is
built by adding a `scene_instance` component to an `SmapEntityRecord`
(`MakeComponentTypeId("scene_instance")`, payload `{ "source": "asset://prefabs/bulb.sscene", "id": "00000000000000F0" }`).

| Test | Scene | Asserts | Fails on main because |
|---|---|---|---|
| `APlacedMemberSpawnsInTheSpawnsGroup` | root + child; child carries a cooked `scene_instance` | `Entities(id).size() == 2`; every member's `SceneInstance.Id` has `SceneInstanceIdRuntimeBit`; `RequestDespawn` destroys both | the child keeps id `0xF0`: `Entities` has 1, and despawn leaves the child |
| `APlacementRootSpawnsAsTheSpawnsRoot` | root and child both carry the cooked stamp (the wrapper variant) | `Root(id)` is valid; an `OwnedByRoot` spawn publishes and follows its root | the group is empty; `Root` is invalid; `OwnedByRoot` fails |

In `test/runtime/NetPrefabSpawnerTests.cpp`, a new test built on
`TempSmapScene` plus an `AssetRecord` with an `Id`, so it does not depend on
the template's cooked pawn:

| Test | Asserts | Fails on main because |
|---|---|---|
| `APlacedMemberJoinsTheNetSpawnsGroup` | `Instantiate` returns the root; the root's `SceneInstance.Source` is the spawned scene's id; `Despawn` destroys every member | a stamped root keeps the inner source, and despawn misses stamped members |

### Fix

1. **Rename `SmapPackageOptions::StripPersistentIdentity` to
   `StripAuthoredIdentity`.**
   - It drops `persistent_id` *and* `scene_instance` payloads in
     `BuildEntityPackageFromSmap` (`SmapFormat.cpp:653-667`).
   - Both are authored identity that a transient spawn must not carry, which
     the option's own comment already states for persistent ids
     (`SmapFormat.h:150-156`).
   - Only the two spawners pass the option (grep), so four lines change.
   - **Why strip rather than replace:** replacing needs a new package mutation
     API (`EntityBuildPackage` has no Set or Remove, `:81-179`) whose only
     caller would be this one. Stripping uses the option both spawners already
     go through.
2. **The stamp becomes a checked add.**
   - A refused `AddComponent<SceneInstance>` fails the spawn:
     `SceneSpawnService` marks the request Failed with an error, and
     `NetPrefabSpawner` refuses once with a log line.
   - Discarding the result is what hid this defect.
3. **Update the contract comment** on `SceneInstance`
   (`SceneInstance.h:17-26`). A cooked placement's members name their
   innermost placement. A runtime spawn's members name the spawn, because the
   placement ids inside a spawned scene are authored identity that the spawn
   strips. The current text promises one meaning for both, which is the
   conflict this defect came from.

**Compatibility:**
- The cooked format is unchanged.
- Level and zone import are unchanged; they don't pass the option.
- No ABI change: `SmapFormat.h` is not a component header.

**Docs:** `docs/plans/body-spawns.md` and
`docs/plans/body-assignment-and-spawn-lifetime.md` describe spawn group
identity. Correct any sentence that the stripping contradicts.

---

## S0.2 Revoke before grant; trace a grant that doesn't fit (A.4.3, A.4.4)

**Invariant.** An effect-held grant is added the pass after its effect applies
and removed the pass after its effect ends (`abilitykit.md`, "Grants and
input"). That invariant must hold when the actor's grant table is full but
an ended effect is freeing room.

**Owner:** `RunAbilityPass` (`engine/src/abilities/AbilitySystem.cpp:289-385`).

**Evidence (read):**
- On the authority, `SyncEffectGrants` runs at `:308-309`, before the actors
  are gathered.
- `RevokeEndedEffectGrants` runs per actor inside the sweep loop at
  `:343-349`.
- `GrantAbility` returns an invalid id when the table is full
  (`AbilityGrants.cpp:11-19`), and `SyncEffectGrants` discards it (`:259-260`).
  Nothing is traced, and the grant is retried every pass.
- No runtime test fills an actor's grants with effect grants (the only
  capacity test is editor-side,
  `test/editor/GameplayVocabularyEditsTests.cpp:176-189`).

### Tests first

These go in `test/runtime/AbilityRuntimeTests.cpp`, using the `AbilityRuntime`
fixture. The effect-grant pattern is from
`AnEffectGrantsItsAbilitiesForAsLongAsItLasts` (`:1196-1221`):
`EffectRegistry::Define`, `DeclareEffectGrants`, `ApplyEffect`, `Step`.

| Test | Steps | Asserts | Fails on main because |
|---|---|---|---|
| `AFullGrantTableSwapsOneEffectsGrantsForAnothersOnOneTick` | 16 Infinite effects, each granting a distinct ability; `Step` (16 grants held); `RemoveEffect(first)` and `ApplyEffect(seventeenth)`; one `Step` | the seventeenth effect's grant is held, and the first's is gone | the grant lands one `Step` later |
| `AnEffectGrantThatDoesNotFitIsTracedOnce` | 16 held; apply a seventeenth; `Step` ×3 | exactly one `GrantRefused` trace naming the actor and the ability; then remove one effect, `Step`, and the grant is held with no further refusal | no trace event exists |

### Fix

1. **Reorder the authority's pass:**
   1. revoke ended effect grants for every granted actor;
   2. `SyncEffectGrants`;
   3. gather actors;
   4. sweep.
   - The revoke runs as its own walk of the `Granted` query. It changes
     component values only, never structure.
   - The sweep loop stops revoking.
   - `Sweep` still follows revocation, so records end `GrantRevoked` on the
     same tick, as today.
2. **Trace a refused effect grant.**
   - Append `AbilityTraceEvent::GrantRefused` (`AbilityTrace.h:28-49`).
   - Trace it once per (effect entity, entry) while the refusal persists.
     `AbilityPassScratch` remembers the refused pairs and forgets one when it
     is granted or its effect ends.
   - The retry behaviour itself is unchanged; making `ApplyEffect` refuse up
     front is Stage 3's reservation.
   - Update the names that switch on the event: `AbilityTrace.cpp:3-17`, and
     `AbilityPreviewInspector.cpp:18-31` and `:73+` in the editor.

**Compatibility:**
- Appending an enum value to `AbilityTraceEvent`: the trace is
  machine-local and never replicated or serialized.
- No wire change.

**Docs:** `abilitykit.md`, "Grants and input" and "Runtime": the pass revokes
before it grants, and a grant that doesn't fit is retried each pass and traced
once.

---

## S0.3 Grant entries told apart by content (A.4.2, A.4.10)

**Invariant.** A grant names the same entry (key, ability, binding) on every
machine.

**Owner:** `AbilityGrantEntries`, together with the `AbilityGrants` wire codec.

**Evidence (read):**
- `Intern` dedupes on the whole entry (`AbilityGrantEntries.cpp:63-73`).
  Content *is* the table's identity.
- The wire carries only `NameWireKey(entry.Key)` (`AbilityGrantsWire.cpp:24`),
  and `FindByWireKey` returns the first entry with that key and ability
  (`:80-86`, its only caller is `AbilityGrantsWire.cpp:47`).
- Entries come from four places:
  - effect definitions (`AbilityEffectGrants.cpp:34`);
  - scene grants (`AbilityGrantsSerializer.cpp:228`);
  - the `ability.grant` verb (`AbilityVerbs.cpp:103`);
  - editor documents.
- Scene-loaded entries interleave by load timing, and a hot reload appends
  (`ContentVocabulary.cpp:145-148,227-240`), so table order differs between
  machines and over time.
- A.4.10: a reload replaces the effect's entry list but never revokes the
  grant from the old entry (`AbilityEffectGrants.cpp:25-41`;
  `AbilitySystem.cpp:264-274`).

### Tests first

| Test | File | Steps | Asserts | Fails on main because |
|---|---|---|---|---|
| `GrantsSharingAKeyAreToldApartOnTheClient` | `test/runtime/AbilityReplicationTests.cpp` (fixture `AbilityReplication`, `ClientPeer = kOwner`) | Intern on both machines, in the same order, two entries with key `g` and the same ability, overrides `{.Priority = 1}` then `{.Priority = 9}`; grant the second on the authority; `Publish()` | the client's grant entry has `Override.Priority == 9` | `FindByWireKey` resolves the first entry, so `Priority == 1` |
| `ReloadingAnEffectsGrantBindingReplacesItsGrant` | `test/runtime/AbilityRuntimeTests.cpp` | Declare an effect granting `charge` bound to `Input.AltFire`; apply; `Step`; declare it again bound to `Input.PrimaryFire`, as a reload does; `Step` | exactly one grant from that effect, bound to `Input.PrimaryFire` | both grants are held (A.4.10) |
| `APawnsOwnerSeedFitsOneSnapshot` | `test/runtime/AbilityReplicationTests.cpp` | Two cases. *Realistic:* Loss Function's pawn loadout (9 grants, 4 attributes, 2 tags, idle records). *Worst case:* every AbilityKit codec at capacity. Each written with budget `kNetMaxSnapshotBytes` | *Realistic:* the entity is written in one snapshot. *Worst case:* the byte count is recorded and asserted only once the owner rules on the result | Not a defect test; it is written **before** the wire change so main's number is on record. If the realistic case fails on main, that is a pre-existing finding and goes to the owner before S0.3 continues. |

### Fix

1. **Wire identity from content.**
   - `AbilityGrantEntries` computes a 64-bit **entry wire key** at `Intern`.
     It is an FNV-1a hash (`core/hash/Fnv1a.h`) over the entry's *names*:
     - the key text;
     - the action name;
     - the enum choices;
     - priority with its presence;
     - the context's tag names.
   - Ability ids and tag ids never enter it, because they are per machine. The
     ability crosses separately, as today.
   - `FindByWireKey(std::uint64_t, AbilityId)` returns the unique match.
   - `Intern` refuses an entry whose wire key equals a *different* existing
     entry's, returning `kAbilityNoGrantEntry` as it does when full. Both
     machines intern the same content, so a collision is refused identically
     on both. A 64-bit key makes that a theoretical case. The existing callers
     already handle `kAbilityNoGrantEntry`.
2. **Wire layout.**
   - `AbilityGrantsWireWords` goes from four words per grant to five: id,
     ability, entry key low, entry key high, kind (`AbilityGrantsWire.h:11-14,
     31-36`).
   - The owner-only grants run grows by 64 bytes at capacity.
3. **Revoke grants whose entry left their effect.**
   - `RevokeEndedEffectGrants` also revokes an Effect-kind grant whose entry
     is no longer in its live effect's `AbilityEffectGrants::Of(def)` list.
   - The next `SyncEffectGrants` adds the current entry.
   - Activations of the revoked grant end `GrantRevoked`, which is acceptable
     for a dev-time reload.
4. **Delete** the key-only `FindByWireKey`.

**Named deviation from the RFC.**
- RFC §B.2 approved a `{origin, key, overlay}` identity. Stage 0 uses a
  content key instead, for two reasons found in the code:
  - **No origin is available at scene load.** Scene-authored grants are
    interned inside the component serializer, and `SceneSerializationContext`
    carries no scene path (`SceneSerializationContext.h:14-25`).
  - **Content is already the table's identity** (`Intern`'s equality). A
    content key fixes A.4.2 and the reload route with no new plumbing.
- Stage 3's overlay entries are interned on the authority at equip time. A
  client must be able to *construct* them, not just find them, so Stage 3's
  plan decides whether those entries also carry an origin.
- **The owner should confirm this deviation.** The alternative is to plumb a
  scene path into `SceneSerializationContext` now.

**Compatibility:**
- **Replication.** `kFrozenReplicationTableHash`
  (`test/core/ComponentIdentityFreezeTests.cpp:177`) is refrozen, with the
  reason in the commit (`DISABLED_PrintFrozenTable` prints the new value).
  Peers on the old build are refused at the handshake by the table hash.
- **Module ABI.** `AbilityGrantsWire.h` and `AbilityGrantEntries.h` are not
  among the fingerprinted headers (`engine/CMakeLists.txt:367-444`).
  - Precedent: the merged AbilityKit moved the replication hash several times
    without bumping `SENCHA_GAME_ABI_VERSION` (28,
    `engine/include/app/ModuleExport.h:23`), and recorded each in
    `docs/architecture/module-abi-history.md`.
  - The same is proposed here: a history note, no bump, unless the owner
    wants one.
- **Scene format.** Unchanged. Scenes save grants by key, ability and
  binding.

**Docs:**
- `abilitykit.md:402-405`: the grant's entry crosses as its content key.
- `module-abi-history.md`: the note above.

**As built (deviations from this plan).**
- **The pawn seed measurement changed the key's width.**
  - Measured on unmodified main, a moving, owned, prefab-spawned pawn with
    nine grants, four attributes and two tags seeds its owner in **1,117 of
    1,173 bytes** (`AShippingPawnsOwnerSeedFitsOneSnapshot`).
  - Five words per grant would add 64 bytes at capacity, which would make that
    pawn unsendable.
  - So the content key is 32 bits in the existing word: the `NameWireKey` of a
    canonical text form of the key and binding. This is the one-word fallback
    the RFC's §B.2 already names.
  - `Intern` refuses a second entry of one ability with the same key.
  - The seed is still 1,117 bytes after the change.
- **No refreeze.** The layout is unchanged, so `kFrozenReplicationTableHash`
  does not move. The word's meaning does, so `kNetProtocolVersion` goes from 7
  to 8. Its contract is "bumped on any wire change".
- **The worst case moves.** The worst-case seed (every codec at capacity) is
  measured by the S0.4 bench rather than held as a test that asserts nothing.

---

## S0.4 Benchmarks (F.2)

These numbers decide D3. **Nothing is optimised in this step.**

### Harness

| Bench | Where | Gate | Script |
|---|---|---|---|
| Entry-shaped entities: ECS and replication | `test/net/EntityReplicationBench.cpp` | `SENCHA_REPLICATION_BENCH_OUT` | `scripts/bench_replication.sh` |
| AbilityKit effect scans | `test/runtime/AbilityBench.cpp` (new scenarios beside the existing ones) | `SENCHA_ABILITY_BENCH_OUT` (existing) | `scripts/bench_abilities.sh` (existing) |

**Mechanics,** following the existing benches:
- profile preset;
- pinned to performance cores (`taskset`, overridable with
  `SENCHA_BENCH_CPUS`);
- `GTEST_SKIP` without the environment variable;
- `Bench::Recorder` JSON and CSV (`test/support/BenchRecorder.h`);
- medians over 240 repetitions after 60 of warm-up.

**Two prerequisite refactors**, both behaviour-preserving:
1. `net_tests` links `test_allocation_counter`, which provides
   `BenchRecorder.h` and `AllocationCount()` (`test/CMakeLists.txt:21-22,
   286-289`).
2. The `Soak` fixture moves from `test/net/ReplicationSoakTests.cpp:113-247`
   into `test/net/ReplicationSoakHarness.h`, shared by the soak tests and the
   bench. The bench times the production `ReplicationRuntime::Publish` rather
   than a copy of its loop.
   - Before reusing it, verify whether the soak clients acknowledge. The
     file's header says they don't, but `Step` sends `AppliedAck()`.

**Stand-in entry.**
- A test component `BenchEntry { EntityId Container; std::uint32_t Definition;
  std::uint32_t Count; std::uint16_t Position; }` with a `TypeSchema`, marked
  replicated.
- It has a `ReplicationCodec` that maps `Container` through the wire context,
  following the `Pointer` pattern (`test/net/ReplicationSnapshotTests.cpp:33-80`).
- Entities carry `BenchEntry`, `PersistentIdComponent` (literal ids `i + 1`,
  with a `PersistentEntityIndex` resource, as `Author()` does at `:2484-2495`)
  and, where replicated, `NetReplicated` + `NetOwner`.

### Measures

| Metric | Method | Sizes |
|---|---|---|
| Bytes per entry, broken down | **Computed:** archetype chunks × 16 KiB ÷ rows; `sizeof(EntityRecord)`; the index, change store, authority identity and per-peer floor maps, counted by element. **Measured:** glibc `mallinfo2().uordblks` delta before and after (Linux only, stated). Both are reported. | 1k / 10k / 50k entries × 0 / 25 / 100 % replicated |
| Create, partition move, destroy | wall time per batch; `ConsumePartitionMoves()` after moves so the journal doesn't skew memory (`World.h:369-408`) | batches of 1 / 100 / 10k |
| Authority publish | median `ReplicationRuntime::Publish` time and `AllocationCount()` delta per publish | replicated entries 0 / 1k / 5k / 10k, idle and with 1 % changing, × 1 and 16 peers |
| Late-join seeding | publishes and simulated wall time until a new peer holds all of a 300-entry container, at the shipping budget | 1 peer joining; and 16 peers |
| Effect scans | median `RunAbilityPass` (authority, which includes `SyncEffectGrants`) and `ResolveAttributesWithEffects`, separately | 600 / 2k / 5k Infinite effects × 0 / 1 / 4 grants each, spread over 100 / 256 actors |
| Owner-matched removal | one `RemoveEffectsOwnedBy` | 600 / 5k effects present |

**Already visible in the code; the bench measures it:**
`ReplicationChangeStore::Update` copies every live entity's state, including
two heap vectors per replicated component, into `LiveEntities` on every
publish, and sorts it (`ReplicationChangeStore.cpp:258-276`). The
allocation-per-publish column shows what that costs per replicated entity. It
is a general replication cost, not an inventory one, and any change to it is
a separate decision for the owner.

### As built

- **The publish driver is not the soak harness.** The soak clients send no
  acknowledgements (their file's header says so), so every entity would ride as
  full state on every publish. The bench drives the functions
  `ReplicationRuntime::Publish` calls, `ReplicationChangeStore::Update` and
  `ReplicationWriteSnapshot` per peer, with explicit acknowledgements, as the
  snapshot tests do.
- **The bench lives in `test/runtime/`.** `runtime_tests` already links the
  recorder, so neither the `net_tests` link change nor the fixture extraction was
  needed.
- **The worst-case pawn seed** is `AbilityReplication.AFullPawnsOwnerSeedIsMeasured`.
  It is skipped without `SENCHA_REPLICATION_BENCH_OUT`, and
  `scripts/bench_replication.sh` records it.

### Recording

- **`docs/plans/evidence/entry-entities/results.md`**, with JSON and CSV
  beside it. It records the method (machine, preset, pinning, medians) and
  every table above. There is no pass or fail verdict; the owner reviews it for
  D3.
- **`abilitykit.md`, "Costs":** the effect-scan table, in the existing format.
- **No budget is set.** The results file states the RFC's representative
  populations (300-entry inventory; 16 × 150 replicated; 200 NPCs × 8, not
  replicated), so the owner can read the numbers at those sizes.

---

## S0.5 Documentation

- `docs/ecs/command-buffers.md:69-80`: `CreateEntity(partition)` returns a
  `PendingEntity` that later commands in the same recording can target
  (`CommandBuffer.h:147-161`). Remove the advice to avoid it.
- The docs each step names above, in the same commit as the step.

---

## Change-path checklist

| Path | Applies | Notes |
|---|---|---|
| ECS or components | lightly | No new components. A test component in the bench. `SceneInstance` contract text. |
| Assets | no | Cooked format unchanged. |
| Editor interaction | lightly | Trace event names in the Workbench inspector. |
| Runtime and rendering | yes | Spawn identity; ability pass order. |
| World partition | no | |
| Public SDK or module boundary | yes | Replication table hash moves (S0.3). No game ABI version bump proposed. |
| Persisted or cooked formats | no | Scene and `.smap` formats are unchanged. |

**Concurrency:** none of the changes touch the job system or async lanes.
The ability pass and spawns run on the owner thread as today.

## Verification

- **Per step:**
  1. the new test, run on main and failing for the stated reason (output kept);
  2. after the fix, the focused test;
  3. `ctest --test-dir build -R '<suite>' --output-on-failure`.
- **Before handoff:**
  - `cmake --preset dev`, `cmake --build --preset dev --parallel`,
    `ctest --preset dev`, `git diff --check`;
  - the `asan` preset for S0.2 and S0.3, which change ability pass ordering and
    grant tables (`ASAN_OPTIONS=detect_leaks=0`).
- **Benchmarks:** both scripts run on the profile preset, pinned. Results are
  committed in S0.4.

## Handoff

The handoff will report:
- the defects reproduced, each with its failing output;
- the fixes;
- the replication hash refreeze;
- the named S0.3 deviation, for the owner's confirmation;
- the benchmark results file;
- anything not run.

Stage 1's plan follows Stage 0's review.
