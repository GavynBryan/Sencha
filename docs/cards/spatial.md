# spatial

Snapshot `97ed48e4` (2026-10-30). Layer: motion and space.

## 1. Frame phases

- None of its own: candidate evaluation and sight tests run synchronously inside their callers, such as `SightScan::Run` (`engine/src/perception/SightScan.cpp:46`).

## 2. Inputs

- Authored evaluations, `CandidateEvaluation` (`engine/include/spatial/candidates/CandidateEvaluation.h:18`), bound by `BindCandidateEvaluation` (`engine/include/spatial/candidates/CandidateEvaluation.h:62`).
- Physics queries and navigation queries, for visibility and reachability measures.
- Module dependencies: assets, authored, core, ecs, gameplay_tags, math, navigation, physics, world.

## 3. Outputs

- Ranked candidates, `CandidateResultBuffer` (`engine/include/spatial/candidates/CandidateScratch.h:60`): consumed by game AI through authored queries.
- `CandidateTrace` (`engine/include/spatial/candidates/CandidateTrace.h:77`): why each candidate passed or failed, consumed by debug views.
- Sight verdicts, `SightResult` (`engine/include/spatial/sight/SightTest.h:32`): consumed by perception.

## 4. Central decisions

- `CandidateEvaluator::Evaluate` (`engine/src/spatial/candidates/CandidateEvaluator.cpp:84`): generates, measures, filters and ranks candidates within budgets.
- `MeasureVisibility` (`engine/src/spatial/candidates/measures/VisibilityMeasure.cpp:40`): decides whether a candidate is seen from or hidden from given entities.
- `TestSight` (`engine/src/spatial/sight/SightTest.cpp:28`): decides whether an observer sees a target within cone, range and occlusion.

## 5. Invariants

- Warmed runs allocate nothing: `CandidateAllocation.WarmedRunsAllocateNothing` (`test/core/CandidateAllocationTests.cpp`).
- An exact search budget runs out in generation order: `CandidateScenario.ExactSearchBudgetRunsOutInGenerationOrder` (`test/spatial/candidates/CandidateScenarioTests.cpp`).
- An airborne querier is reported, not silently empty: `CandidateScenario.AnAirborneQuerierIsReportedNotSilentlyEmpty` (`test/spatial/candidates/CandidateScenarioTests.cpp`).
- Out-of-sight placement spends rays only inside cone and range: `CandidateScenario.OutOfSightPlacementSpendsRaysOnlyInsideConeAndRange` (`test/spatial/candidates/CandidateScenarioTests.cpp`).
- A zone without navigation still answers geometric and tag questions: `CandidateScenario.AZoneWithoutNavigationStillAnswersGeometricAndTagQuestions` (`test/spatial/candidates/CandidateScenarioTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 4,588 lines in 45 files.
- CCN 24 `MeasureRouteVisibility` (`engine/src/spatial/candidates/measures/RouteVisibilityMeasure.cpp:64`); CCN 22 `MeasureVisibility` (`engine/src/spatial/candidates/measures/VisibilityMeasure.cpp:40`); CCN 20 `CandidateEvaluator::Evaluate` (`engine/src/spatial/candidates/CandidateEvaluator.cpp:84`).

## 7. Docs that contradict the code

- None found.
