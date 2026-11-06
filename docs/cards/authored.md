# authored

Snapshot `97ed48e4` (2026-10-30). Layer: gameplay simulation.

## 1. Frame phases

- `Simulate`: `Drain` (`engine/src/app/EngineFramePhases.cpp:794`) delivers queued authored events after the FixedLogic lane each tick.
- Verbs and queries dispatch synchronously from whichever system invokes them; the module schedules no system.

## 2. Inputs

- Declarations from every module and game, committed through `AuthoredVocabularyScope` (`engine/include/authored/AuthoredApi.h:90`).
- Authored values from content, checked by `ValidateAuthoredField` (`engine/include/authored/AuthoredSchema.h:22`).
- Module dependencies: assets, core, ecs, gameplay_tags, math, world.

## 3. Outputs

- `VerbDispatcher` (`engine/include/authored/VerbDispatcher.h:47`): invokes bound operations, held by abilities, anim, app, camera, logic and ui.
- `AuthoredQueryDispatcher` (`engine/include/authored/AuthoredQueryDispatcher.h:23`): read-only answers, consumed by abilities, logic, navigation, perception and spatial.
- `AuthoredEventDispatcher` (`engine/include/authored/AuthoredEventDispatcher.h:84`): buffered occurrences, consumed by responses and subscribers.
- `AuthoredCatalog` (`engine/include/authored/AuthoredCatalog.h:153`): the vocabulary the editor's catalog view lists.

## 4. Central decisions

- `VerbDispatcher::Dispatch` (`engine/src/authored/VerbDispatcher.cpp:207`): runs a bound operation or returns the reason it refused.
- `Drain` (`engine/include/authored/AuthoredEventDispatcher.h:128`): delivers occurrences FIFO until the queue empties or the budget is spent.
- `AuthoredValueSatisfiesField` (`engine/src/authored/AuthoredValue.cpp:221`): decides whether a value fits a declared field.
- `ValidateAuthoredField` (`engine/src/authored/AuthoredSchema.cpp:90`): accepts or refuses a field declaration.

## 5. Invariants

- Publishing only queues: `AuthoredEventDispatchTest.PublishingOnlyQueues` (`test/core/AuthoredEventDispatchTests.cpp`).
- A chain of reactions completes in one drain without nesting: `AuthoredEventDispatchTest.AChainOfReactionsCompletesInOneDrainWithoutNesting` (`test/core/AuthoredEventDispatchTests.cpp`).
- A full queue refuses rather than drops: `AuthoredEventDispatchTest.AFullQueueRefusesRatherThanDropping` (`test/core/AuthoredEventDispatchTests.cpp`).
- A warmed publish and drain allocate nothing: `AuthoredEventDispatchTest.AWarmedPublishAndDrainAllocateNothing` (`test/core/AuthoredEventDispatchTests.cpp`).
- Dispatch refuses to re-enter itself: `VerbDispatchTest.DispatchRefusesToReenterItself` (`test/core/VerbDispatchTests.cpp`).
- A conflict in one catalog publishes nothing in any: `VocabularyScopeTest.AConflictInOneCatalogPublishesNothingInAny` (`test/core/AuthoredCatalogTests.cpp`).
- Narrowing is refused, not wrapped: `AuthoredValueTraits.NarrowingIsRefusedNotWrapped` (`test/core/AuthoredValueTraitsTests.cpp`).
- A scope mismatch is refused at runtime: `VerbLeaseTest.ScopeMismatchIsRefusedAtRuntime` (`test/core/VerbLeaseTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 5,997 lines in 49 files.
- CCN 39 `AuthoredValueSatisfiesField` (`engine/src/authored/AuthoredValue.cpp:221`); CCN 31 `ValidateAuthoredField` (`engine/src/authored/AuthoredSchema.cpp:90`); CCN 31 `VerbDispatcher::Dispatch` (`engine/src/authored/VerbDispatcher.cpp:207`).

## 7. Docs that contradict the code

- None found.
