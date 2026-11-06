# prediction

Snapshot `97ed48e4` (2026-10-30). Layer: network.

## 1. Frame phases

- `PumpNet`: `ReplayPawnState` (`engine/src/app/EngineFramePhases.cpp:568`) re-simulates the local pawn from the authority's state after each applied snapshot.
- The module schedules no system; abilities and anim reconcile their own guesses with `PredictionJournal` (`engine/include/prediction/PredictionJournal.h:12`).

## 2. Inputs

- A replay request, `PawnReplayRequest` (`engine/include/prediction/PawnStateReplay.h:65`): the acknowledged command tick, the commands since, and the authority's pawn state.
- Module dependencies: ecs, math, movement, net, physics, time, world.

## 3. Outputs

- `PawnReplayResult` (`engine/include/prediction/PawnStateReplay.h:94`): where the replayed pawn ended; the engine's only call to `ReplayPawnState` (`engine/src/app/EngineFramePhases.cpp:568`) discards it.
- Corrected pawn components in the World: consumed by movement on the next tick.
- `PredictionJournal` decisions: consumed by abilities and anim to keep or retract guesses.

## 4. Central decisions

- `ReplayPawnState` (`engine/src/prediction/PawnStateReplay.cpp:26`): restores the authority's state and re-runs every unacknowledged command.
- `PredictionJournal::Reconcile` (`engine/include/prediction/PredictionJournal.h:48`): splits guesses into decided and undecided at the acknowledged tick.
- `CollectUnresumedPredictedComponents` (`engine/src/prediction/PawnStateReplay.cpp:15`): finds predicted components the snapshot did not resume.

## 5. Invariants

- A guess is decided only once its command has run: `PredictionJournal.AGuessIsDecidedOnlyOnceItsCommandHasRun` (`test/net/PredictionJournalTests.cpp`).
- Past its capacity nothing more is guessed: `PredictionJournal.PastItsCapacityNothingMoreIsGuessed` (`test/net/PredictionJournalTests.cpp`).
- A client driven into geometry converges under loss: `PredictionConvergence.AClientDrivenIntoGeometryConvergesUnderLoss` (`test/net/PredictionConvergenceTests.cpp`).
- A jump through reconciles is exactly one jump: `PredictionConvergence.AJumpThroughReconcilesIsExactlyOneJump` (`test/net/PredictionConvergenceTests.cpp`).
- Replay reaches exactly where the live ticks reached: `PawnStateReplay.ReachesExactlyWhereTheLiveTicksReached` (`test/net/PawnStateReplayTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 328 lines in 3 files.
- CCN 18 `ReplayPawnState` (`engine/src/prediction/PawnStateReplay.cpp:26`); CCN 4 `PredictionJournal::Reconcile` (`engine/include/prediction/PredictionJournal.h:48`); CCN 4 `CollectUnresumedPredictedComponents` (`engine/src/prediction/PawnStateReplay.cpp:15`).

## 7. Docs that contradict the code

- None found.
