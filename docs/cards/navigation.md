# navigation

Snapshot `97ed48e4` (2026-10-30). Layer: motion and space.

## 1. Frame phases

- `ZoneResidency`: `NavigationSystem::ZoneResidency` (`engine/include/navigation/NavigationSystem.h:30`) attaches and detaches each zone's navigation as zones change.
- `Simulate` (PostFixed lane): `NavigationSystem::PostFixed` (`engine/include/navigation/NavigationSystem.h:31`) runs after the fixed tick; registered as `NavigationSystem` (`engine/src/app/Engine.cpp:1413`).
- `Simulate` (FixedLogic lane): `NavAgentSystem` (`engine/include/navigation/NavAgentSystem.h:53`) plans and follows routes; `RegisterNavigationAgents` (`templates/fps/src/FpsGame.cpp:113`) is called by the game, not the engine.

## 2. Inputs

- Cooked navigation per zone, `NavigationFile` (`engine/include/navigation/NavigationFile.h:72`), read by `ReadZoneNavigationFile` (`engine/include/navigation/ZoneNavigation.h:89`).
- Agent orders, `NavAgentRequest` (`engine/include/navigation/NavAgent.h:61`), and authored policies, `NavigationPolicyData` (`engine/include/navigation/NavigationPolicyData.h:32`).
- Module dependencies: app, assets, authored, core, debug, ecs, gameplay_tags, jobs, math, movement, participant, physics, time, world, zone.

## 3. Outputs

- `NavAgentProgress` (`engine/include/navigation/NavAgent.h:184`): phase, failure and repath reason, consumed by game AI and debug.
- Movement intent on agents: consumed by movement.
- Queries, `NavFindRoute` (`engine/include/navigation/NavigationQuery.h:49`) and `NavReachable` (`engine/include/navigation/NavigationQuery.h:33`): consumed by spatial candidates and authored queries.
- Cooked tiles, `BuildNavTile` (`engine/include/navigation/NavTileBuild.h:104`): consumed by the level cook.

## 4. Central decisions

- `NavAgentSystem::State::Follow` (`engine/src/navigation/NavAgentSystem.cpp:712`): steers an agent along its route and decides arrival, stuck or repath.
- `NavAgentSystem::State::Sweep` (`engine/src/navigation/NavAgentSystem.cpp:419`): checks whether an agent's route is still valid.
- `NavPlanQueue` (`engine/include/navigation/NavPlanQueue.h:31`): decides which plans run within this tick's budget.

## 5. Invariants

- Plans are taken by tick, then entity: `NavPlanQueue.TakesByTickThenEntity` (`test/navigation/NavPlanQueueTests.cpp`).
- Blocking work goes first but cannot starve a refresh: `NavPlanQueue.BlockingWorkGoesBeforeARecentRefreshButCannotStarveIt` (`test/navigation/NavPlanQueueTests.cpp`).
- Identical input produces an identical cooked file: `NavigationCook.IdenticalInputProducesIdenticalFile` (`test/navigation/NavigationCookTests.cpp`).
- Corruption and version mismatches are rejected: `NavigationFile.CorruptionAndVersionAreRejected` (`test/navigation/NavigationCookTests.cpp`).
- An agent never passes the leg's last corner: `NavRouteFollowing.NeverPassesTheLegsLastCorner` (`test/navigation/NavRouteFollowingTests.cpp`).
- An unhandled crossing is never planned: `NavAgentTraversal.AnUnhandledCrossingIsNeverPlanned` (`test/navigation/NavAgentTraversalTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 7,454 lines in 65 files.
- CCN 30 `NavAgentSystem::State::Follow` (`engine/src/navigation/NavAgentSystem.cpp:712`); CCN 28 `NavAgentSystem::State::Sweep` (`engine/src/navigation/NavAgentSystem.cpp:419`); CCN 19 `RegisterNavigationAgentConsole` (`engine/src/navigation/NavAgentConsole.cpp:56`).

## 7. Docs that contradict the code

- `docs/navigation/runtime.md:67` lists `NavTileBuilder` as a type; no such symbol exists (the builder is the function `BuildNavTile`).
