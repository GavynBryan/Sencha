# perception

Snapshot `97ed48e4` (2026-10-30). Layer: gameplay simulation.

## 1. Frame phases

- `Simulate` (FixedLogic lane): `PerceptionSystem` (`engine/include/perception/PerceptionSystem.h:53`) scans sight each tick; readers order themselves after it.
- `RegisterPerception` (`templates/fps/src/FpsGame.cpp:90`) is the only call site: the engine does not schedule perception itself.

## 2. Inputs

- Observers and candidates in the World, gathered through spatial queries and physics rays by `SightScan::Run` (`engine/src/perception/SightScan.cpp:46`).
- Module dependencies: app, authored, core, debug, ecs, gameplay_tags, math, physics, spatial, world.

## 3. Outputs

- `PerceptionFrame` (`engine/include/perception/PerceptionFrame.h:22`): this tick's `Observation` records, consumed by perception queries and game AI.
- `Observation` (`engine/include/perception/Observation.h:11`): evidence that an observer acquired something, with scan coverage.
- Authored queries, `BindPerceptionQueries` (`engine/include/perception/PerceptionVocabulary.h:36`): consumed by response and ability conditions.
- `SightStats` (`engine/include/perception/PerceptionSystem.h:15`): consumed by debug drawing.

## 4. Central decisions

- `SightScan::Run` (`engine/src/perception/SightScan.cpp:46`): decides which candidates each observer sees this tick and whether the scan was complete.
- `NearestSeenTagged` (`engine/src/perception/PerceptionVocabulary.cpp:109`): answers the nearest seen entity under a tag, or unavailable.

## 5. Invariants

- A partial scan never answers false: `PerceptionQueries.APartialScanNeverAnswersFalse` (`test/spatial/perception/PerceptionQueryTests.cpp`).
- Scans are ordered by observer then sense whatever order passes ran: `PerceptionFrame.ScansAreOrderedByObserverThenSenseWhateverOrderPassesRan` (`test/spatial/perception/PerceptionFrameTests.cpp`).
- Coverage is complete only when every scan finished: `PerceptionFrame.CoverageIsCompleteOnlyWhenEveryScanFinished` (`test/spatial/perception/PerceptionFrameTests.cpp`).
- Begin forgets the last tick: `PerceptionFrame.BeginForgetsTheLastTick` (`test/spatial/perception/PerceptionFrameTests.cpp`).

### Unchecked

- `PerceptionSystem::FixedLogic` gates on `IsSimulationAuthority` (`engine/src/perception/PerceptionSystem.cpp:130`); no test runs it as a client.

## 6. Size

- 927 lines in 11 files.
- CCN 23 `SightScan::Run` (`engine/src/perception/SightScan.cpp:46`); CCN 14 `DrawSight` (`engine/src/perception/PerceptionSystem.cpp:56`); CCN 13 `NearestSeenTagged` (`engine/src/perception/PerceptionVocabulary.cpp:109`).

## 7. Docs that contradict the code

- None found.
