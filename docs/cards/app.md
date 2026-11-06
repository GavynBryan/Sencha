# app

Snapshot `97ed48e4` (2026-10-30). Layer: world and frame.

## 1. Frame phases

- Every phase: `Engine::RegisterFramePhases` (`engine/src/app/EngineFramePhases.cpp:105`) registers all engine work into the `FrameDriver`, and `EngineSchedule` lanes are run from those registrations.
- `ExtractRender`: `RunExtractRender` (`engine/src/app/EngineFramePhases.cpp:1151`) runs `DefaultRenderPipeline` (`engine/src/app/Engine.cpp:174`).
- `ScheduleTicks`: `RunPreSimulate` (`engine/src/app/EngineFramePhases.cpp:768`) runs `PauseInputSystem` (`engine/src/app/Engine.cpp:1407`) when the application shell is on.

## 2. Inputs

- A `Game` implementation (`engine/include/app/Game.h:24`), in-tree or loaded as a module by `GameModuleLoader` (`engine/include/app/GameModuleLoader.h:39`).
- Engine config, content roots and the command line, through `RuntimeContent` (`engine/include/app/RuntimeContent.h:40`).
- Module dependencies: 33 engine modules; app is the integration root.

## 3. Outputs

- `Engine` (`engine/include/app/Engine.h:83`): owns services, the frame loop, the unified world, the schedule and worker lanes; consumed by games and editors (28 editor files).
- `EngineSchedule` (`engine/include/app/EngineSchedule.h:64`): the ordered system list every engine module registers into.
- Phase contexts such as `ZoneResidencyContext` (`engine/include/app/GameContexts.h:74`): the structs scheduled systems receive.
- The pause shell, `PauseState` (`engine/include/app/PauseState.h:47`) and `PauseMenuModel` (`engine/include/app/PauseMenuModel.h:64`): consumed by runtime UI.

## 4. Central decisions

- `Engine::Run` (`engine/src/app/Engine.cpp:1014`): startup, the frame loop and shutdown for a game.
- `EngineSchedule::Init` (`engine/include/app/EngineSchedule.h:87`): resolves declared ordering edges into each lane's run order.
- `DescribeGameModuleAbiMismatch` (`engine/src/app/GameModuleLoader.cpp:38`): accepts or refuses a game module by comparing ABI fingerprints.
- `PauseState::Apply` (`engine/include/app/PauseState.h:87`): resolves requested pause transitions into one edge per frame.

## 5. Invariants

- Ordering edges apply within a lane: `EngineScheduleTest.DependencyOrderingAppliesWithinPhase` (`test/core/EngineScheduleTests.cpp`).
- Init runs in registration order and shutdown in reverse: `EngineScheduleTest.InitAndShutdownUseRegistrationAndReverseOrder` (`test/core/EngineScheduleTests.cpp`).
- An edge naming an unregistered system is fatal: `EngineScheduleOrderingDeath.AnEdgeNamingAnUnregisteredSystemDies` (`test/core/EngineScheduleTests.cpp`).
- A module with a different ABI is refused: `GameModuleLoader.RefusesAbiMismatch` (`test/runtime/GameModuleLoaderTests.cpp`).
- The module descriptor layout is frozen: `ModuleAbi.DescriptorLayoutIsFrozen` (`test/runtime/ModuleAbiLayoutTests.cpp`).
- Repeated pause requests coalesce into one transition: `PauseStateTest.RepeatedRequestsCoalesceIntoOneTransition` (`test/runtime/PauseStateTests.cpp`).
- Back-routing bands hold regardless of registration order: `BackRouterTest.BandOrderHoldsRegardlessOfRegistrationOrder` (`test/runtime/BackRouterTests.cpp`).

### Unchecked

- A Release game module loads against a Release engine: the fingerprint folds in the debug bit (`engine/include/app/GameModuleAbi.h:89`), and `.github/workflows/ci.yml` builds only Debug.

## 6. Size

- 10,265 lines in 51 files.
- CCN 64 `Engine::Run` (`engine/src/app/Engine.cpp:1014`); CCN 62 `Engine::RegisterNetFramePhases` (`engine/src/app/EngineFramePhases.cpp:168`); CCN 42 `Engine::RegisterPresentationFramePhases` (`engine/src/app/EngineFramePhases.cpp:958`).

## 7. Docs that contradict the code

- `docs/gameplay/pause.md:306` says `UiPreviewModelTests` checks the shell sidecar; no such test exists in the tree.
