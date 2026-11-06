# jobs

Snapshot `97ed48e4` (2026-10-30). Layer: foundation.

## 1. Frame phases

- `DrainAsyncTasks`: completed background work commits at `engine/src/app/EngineFramePhases.cpp:713`, through `DrainCompletions` on line 720.
- Fork-join work runs inside whichever phase calls `ParallelFor`; the frame pool is a `JobSystem` built in `engine/src/app/Engine.cpp:228`.

## 2. Inputs

- Work functions and commit callbacks, through `AsyncTaskQueue::Submit` (`engine/include/jobs/AsyncTaskQueue.h:112`).
- Index-ranged jobs, through `JobSystem::ParallelFor` (`engine/include/jobs/JobSystem.h:65`).
- A per-drain budget, `AsyncDrainBudget` (`engine/include/jobs/AsyncTaskQueue.h:62`), read from `config.Runtime.AsyncCommitBudgetMs`.
- Module dependencies: none.

## 3. Outputs

- Commit callbacks run on the owner thread at the drain point: consumed by zone loading, asset staging and navigation rebuilds (users: anim, app, assets, navigation, runtime, zone).
- Task state, `AsyncTaskHandle` (`engine/include/jobs/AsyncTaskQueue.h:33`): read by callers to cancel or poll.

## 4. Central decisions

- `AsyncTaskQueue::DrainCompletions` (`engine/src/jobs/AsyncTaskQueue.cpp:105`): decides which finished tasks commit this frame, in completion order, within the budget.
- `AsyncTaskQueue::Cancel` (`engine/src/jobs/AsyncTaskQueue.cpp:88`): decides whether a task's work or commit is dropped, depending on how far it got.
- `JobSystem::ParallelFor` (`engine/include/jobs/JobSystem.h:65`): splits a batch across workers with the caller participating; with zero workers it runs inline in index order.

## 5. Invariants

- Zero workers run every job inline in index order: `JobSystemZeroWorkers.RunsAllJobsInlineInIndexOrder` (`test/jobs/JobSystemTests.cpp`).
- Every index executes exactly once: `JobSystemThreaded.EveryIndexExecutesExactlyOnce` (`test/jobs/JobSystemTests.cpp`).
- Nested `ParallelFor` is refused: `JobSystemContracts.NestedParallelForDies` (`test/jobs/JobSystemTests.cpp`).
- Commits run in completion order with their payload: `AsyncTaskQueueZeroThread.DrainRunsCommitsWithPayloadInCompletionOrder` (`test/jobs/AsyncTaskQueueTests.cpp`).
- The commit-count budget is a hard cap: `AsyncTaskQueueZeroThread.CommitCountBudgetIsAHardCap` (`test/jobs/AsyncTaskQueueTests.cpp`).
- Cancelling after work drops the commit: `AsyncTaskQueueZeroThread.CancelAfterPumpDropsCommit` (`test/jobs/AsyncTaskQueueTests.cpp`).

### Unchecked

- "No raw threads, no `std::async`, no third pool" (CLAUDE.md) has no ctest; `SwapchainRebuildWorker` owns a `std::thread` (`engine/include/runtime/SwapchainRebuildWorker.h`).

## 6. Size

- 633 lines in 4 files.
- CCN 7 `AsyncTaskQueue::DrainCompletions` (`engine/src/jobs/AsyncTaskQueue.cpp:105`); CCN 6 `AsyncTaskQueue::Cancel` (`engine/src/jobs/AsyncTaskQueue.cpp:88`); CCN 5 `AsyncTaskQueue::Stop` (`engine/src/jobs/AsyncTaskQueue.cpp:45`).

## 7. Docs that contradict the code

- None found.
