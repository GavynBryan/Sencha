# participant_spawn

Snapshot `97ed48e4` (2026-10-30). Layer: network.

## 1. Frame phases

- None of its own: games call `Update` (`engine/include/participant_spawn/ParticipantBodySpawns.h:49`) each frame, as `templates/fps/src/PawnSpawn.cpp:230` does.

## 2. Inputs

- Spawn requests per participant, `ParticipantBodySpawnRequest` (`engine/include/participant_spawn/ParticipantBodySpawns.h:15`), started by `StartSpawn` (`engine/include/participant_spawn/ParticipantBodySpawns.h:48`).
- Landed prefab groups from the runtime's scene spawn service.
- Module dependencies: core, ecs, participant, runtime.

## 3. Outputs

- A body assigned to each participant through `ParticipantLifecycle` (`engine/include/participant/ParticipantLifecycle.h:98`): consumed by the game's pawn code in each template.
- Despawned groups for refused or cancelled attempts.

## 4. Central decisions

- `ParticipantBodySpawns::Settle` (`engine/src/participant_spawn/ParticipantBodySpawns.cpp:75`): assigns, keeps or despawns a landed group.
- `ParticipantBodySpawns::Update` (`engine/src/participant_spawn/ParticipantBodySpawns.cpp:53`): settles every attempt whose spawn has landed this frame.
- `ParticipantBodySpawns::StartSpawn` (`engine/src/participant_spawn/ParticipantBodySpawns.cpp:36`): starts or refuses a spawn attempt for a participant.

## 5. Invariants

- A failed spawn is consumed without a retry storm: `ParticipantBodySpawns.AFailedSpawnIsConsumedWithoutARetryStorm` (`test/runtime/ParticipantBodySpawnsTests.cpp`).
- An old completion never binds to a replacement attempt: `ParticipantBodySpawns.AnOldCompletionNeverBindsToAReplacementAttempt` (`test/runtime/ParticipantBodySpawnsTests.cpp`).
- A group with two roots is refused before it is published: `ParticipantBodySpawns.AGroupWithTwoRootsIsRefusedBeforeItIsPublished` (`test/runtime/ParticipantBodySpawnsTests.cpp`).
- Landings in one frame are assigned in request order: `ParticipantBodySpawns.LandingsInOneFrameAreAssignedInRequestOrder` (`test/runtime/ParticipantBodySpawnsTests.cpp`).
- Retirement reaps the whole group: `ParticipantBodySpawns.RetirementReapsTheWholeGroup` (`test/runtime/ParticipantBodySpawnsTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 232 lines in 2 files.
- CCN 14 `ParticipantBodySpawns::Settle` (`engine/src/participant_spawn/ParticipantBodySpawns.cpp:75`); CCN 9 `ParticipantBodySpawns::Update` (`engine/src/participant_spawn/ParticipantBodySpawns.cpp:53`); CCN 5 `ParticipantBodySpawns::StartSpawn` (`engine/src/participant_spawn/ParticipantBodySpawns.cpp:36`).

## 7. Docs that contradict the code

- None found.
