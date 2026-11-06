# logic

Snapshot `97ed48e4` (2026-10-30). Layer: gameplay simulation.

## 1. Frame phases

- `Simulate` (FixedLogic lane): `ResponsesSystem::FixedLogic` (`engine/include/logic/ResponsesSystem.h:129`) runs queued responses each tick.
- `ZoneResidency`: `ResponsesSystem::ZoneResidency` (`engine/include/logic/ResponsesSystem.h:130`) keeps and restores response state as zones change.
- `Simulate` (FixedLogic lane): `VerbRelaySystem::FixedLogic` (`engine/include/logic/VerbRelaySystem.h:75`) drains relay activations.
- `RegisterVerbRelaySystem` (`engine/src/app/Engine.cpp:1392`) and `RegisterResponsesSystem` (`engine/src/app/Engine.cpp:1400`) are the engine's call sites.

## 2. Inputs

- Entities' authored responses, `Responses` (`engine/include/logic/Responses.h:24`), and their sets, `ResponseSetStore` (`engine/include/logic/ResponseSet.h:67`).
- Trigger-volume overlaps and interaction requests, `SensorInteractions` (`engine/include/logic/ResponsesSystem.h:46`).
- Authored occurrences, through `Judge` (`engine/include/logic/ResponsesSystem.h:118`).
- Module dependencies: app, assets, authored, controller, core, ecs, gameplay_tags, math, physics, time, world, zone.

## 3. Outputs

- Verb invocations, through `VerbSequenceRunner` (`engine/include/logic/VerbSequenceRunner.h:39`): consumed by every module that declares verbs.
- Use targets, `FindUseCandidate` (`engine/include/logic/InteractUse.h:48`): consumed by the interact verb, `InteractUseOperations` (`engine/include/logic/InteractUse.h:53`).
- Compiled sequences, `CompileVerbSequence` (`engine/include/logic/VerbSequenceCompiler.h:26`): consumed by responses and relays.

## 4. Central decisions

- `ResponsesSystem::RunQueue` (`engine/src/logic/ResponsesSystem.cpp:379`): runs admitted activations in order within the tick's budget.
- `ResponsesSystem::Judge` (`engine/src/logic/ResponsesSystem.cpp:171`): decides whether an occurrence finds a ready binding on its subject.
- `SequenceCompilation::Value` (`engine/src/logic/VerbSequenceCompiler.cpp:263`): resolves one authored value against ports, queries and constants.

## 5. Invariants

- Many activators run in identity order and commands in authored order: `ResponsesFixture.ManyActivatorsRunInIdentityOrderAndCommandsInAuthoredOrder` (`test/runtime/ResponsesTests.cpp`).
- A fast crossing still enters and exits: `ResponsesFixture.AFastCrossingStillEntersAndExits` (`test/runtime/ResponsesTests.cpp`).
- A client runs nothing: `ResponsesFixture.AClientRunsNothing` (`test/runtime/ResponsesTests.cpp`).
- Activations past the budget wait for the next pass in order: `ResponsesFixture.ActivationsPastTheBudgetWaitForTheNextPassInOrder` (`test/runtime/ResponsesTests.cpp`).
- A relay queue is bounded and overflow is refused visibly: `VerbRelayFixture.TheQueueIsBoundedAndOverflowIsRefusedVisibly` (`test/runtime/VerbRelayTests.cpp`).
- Only the authority uses: `InteractUse.OnlyTheAuthorityUses` (`test/runtime/InteractUseTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 4,223 lines in 30 files.
- CCN 22 `ResponsesSystem::RunQueue` (`engine/src/logic/ResponsesSystem.cpp:379`); CCN 19 `ResponsesSystem::Judge` (`engine/src/logic/ResponsesSystem.cpp:171`); CCN 18 `SequenceCompilation::Value` (`engine/src/logic/VerbSequenceCompiler.cpp:263`).

## 7. Docs that contradict the code

- None found.
