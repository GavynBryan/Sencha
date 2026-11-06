# net

Snapshot `97ed48e4` (2026-10-30). Layer: network.

## 1. Frame phases

- `PumpNet`: registered at `engine/src/app/EngineFramePhases.cpp:177`; receives datagrams, admits peers and applies snapshots through `Apply` (`engine/src/app/EngineFramePhases.cpp:521`) before the tick that reads them.
- `FlushNet`: registered at `engine/src/app/EngineFramePhases.cpp:620`; writes snapshots per peer through `Publish` (`engine/src/app/EngineFramePhases.cpp:636`).
- `Simulate` (FixedLogic lane): `PeerCommandFeedSystem` (`engine/src/net/PeerCommandRuntime.cpp:226`), `ReplicationInterpolationSystem` (`engine/src/net/PeerCommandRuntime.cpp:227`) and `PawnCommandCaptureSystem` (`engine/src/net/PeerCommandRuntime.cpp:228`).
- `RegisterNetSystems` (`templates/arena/src/ArenaGame.cpp:118`) is called only by the arena template.

## 2. Inputs

- Datagrams through `INetTransport` (`engine/include/net/NetTransport.h:41`), with `LoopbackTransport` (`engine/include/net/LoopbackTransport.h:50`) for tests.
- Entities marked `NetReplicated` (`engine/include/net/NetReplicationComponents.h:29`), and the replicated component table built from each component's wire form.
- Local commands, `NetPlayerCommand` (`engine/include/net/NetPlayerCommand.h:71`), captured from input actions.
- Module dependencies: abilities, app, controller, core, ecs, input, math, movement, physics, world, zone.

## 3. Outputs

- `ReplicationRuntime` (`engine/include/net/ReplicationRuntime.h:78`): applies and publishes replicated state; driven by app's net phases and read by debug.
- `NetSession` (`engine/include/net/NetSession.h:145`): roles, peers and admission, consumed by app, debug and three game templates.
- `NetOwnerOf` (`engine/include/net/NetOwnership.h:52`): which peer owns an entity, consumed inside net by owner-only replication.
- `NetStats` (`engine/include/net/NetStats.h:70`): traffic by kind, consumed by app and debug.

## 4. Central decisions

- `ReplicationApplySnapshot` (`engine/src/net/ReplicationSnapshot.cpp:865`): applies one snapshot to a client World, creating, updating, holding and destroying entities.
- `ReplicationWriteSnapshot` (`engine/src/net/ReplicationSnapshot.cpp:594`): decides what each peer is sent, from acknowledgements, ownership and zone scope.
- `NetSession` (`engine/include/net/NetSession.h:145`): admits or refuses a peer on build, content, table and tick-rate identity.

## 5. Invariants

- Unmarked entities are not replicated: `ReplicationSnapshot.UnmarkedEntitiesAreNotReplicated` (`test/net/ReplicationSnapshotTests.cpp`).
- Owner-only state reaches only its owner: `ReplicationSnapshot.OwnerOnlyStateReachesOnlyItsOwner` (`test/net/ReplicationSnapshotTests.cpp`).
- A lost snapshot is not promoted by a later one: `ReplicationAcknowledgement.ALostSnapshotIsNotPromotedByALaterOne` (`test/net/ReplicationSnapshotTests.cpp`).
- A still World costs nothing after the first snapshot: `ReplicationSnapshot.AStillWorldCostsNothingAfterTheFirstSnapshot` (`test/net/ReplicationSnapshotTests.cpp`).
- The authority refuses a mismatched replicated table: `NetSession.AuthorityRefusesAMismatchedReplicatedTable` (`test/net/NetSessionTests.cpp`).
- A reliable duplicate is not delivered twice: `NetChannel.ReliableDuplicateIsNotDeliveredTwice` (`test/net/NetChannelTests.cpp`).
- Padding is not folded into the desync hash: `NetDesyncFold.PaddingIsNotFolded` (`test/net/NetDesyncProbeTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 14,945 lines in 70 files.
- CCN 129 `ReplicationApplySnapshot` (`engine/src/net/ReplicationSnapshot.cpp:865`), 506 lines long: the most complex function in the engine; CCN 43 `ReplicationWriteSnapshot` (`engine/src/net/ReplicationSnapshot.cpp:594`); CCN 35 `RegisterNetConsoleCommands` (`engine/src/net/NetConsoleCommands.cpp:133`).

## 7. Docs that contradict the code

- None found.
