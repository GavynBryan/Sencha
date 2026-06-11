#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

//=============================================================================
// AsyncTaskState / AsyncTaskHandle
//
// A handle observes one submitted task. State advances strictly:
//   Pending -> Running -> AwaitingCommit -> Committed
// Cancel diverts Pending or AwaitingCommit to Cancelled; a Running task
// cannot be cancelled (its work is already executing) — callers may retry
// after it reaches AwaitingCommit.
//=============================================================================
enum class AsyncTaskState : uint8_t
{
    Pending,         // queued; work has not started
    Running,         // work executing on a task thread (or inside PumpWork)
    AwaitingCommit,  // work finished; commit waits for DrainCompletions
    Committed,       // commit ran on the owner thread; the task is done
    Cancelled,       // cancelled before its commit ran; commit never runs
};

class AsyncTaskHandle
{
public:
    AsyncTaskHandle() = default;
    [[nodiscard]] bool IsValid() const { return State != nullptr; }

private:
    friend class AsyncTaskQueue;
    explicit AsyncTaskHandle(std::shared_ptr<std::atomic<AsyncTaskState>> state)
        : State(std::move(state))
    {
    }

    std::shared_ptr<std::atomic<AsyncTaskState>> State;
};

//=============================================================================
// AsyncDrainBudget
//
// Limits one DrainCompletions call. The two axes have distinct semantics:
//
//   MaxCommits — hard cap. 0 is valid and drains nothing (an explicit pause;
//   tests also use small counts for deterministic stepping).
//
//   MaxTime — soft wall-time cap for frame pacing. The first ready commit
//   always runs (so completions can never starve), and no further commit
//   starts once the elapsed time exceeds the budget. A budget cannot split a
//   commit: commits are atomic by design (publish-by-handoff), so a consumer
//   with a large payload — a multi-megabyte GPU upload, say — must shape it
//   as multiple chunked tasks; the budget then meters between chunks.
//=============================================================================
struct AsyncDrainBudget
{
    static constexpr std::size_t NoLimit = static_cast<std::size_t>(-1);

    std::size_t MaxCommits = NoLimit;
    std::chrono::steady_clock::duration MaxTime =
        std::chrono::steady_clock::duration::max();
};

//=============================================================================
// AsyncTaskQueue
//
// Async (cross-frame) lane of the job design — see docs/ecs/parallelization.md,
// Decision 3. Long-running work (IO, decode, detached registry builds) runs on
// dedicated task threads; its result re-enters engine state only through the
// commit callback, which runs on the owner thread at the per-frame drain point.
// Until commit runs, a task's result is plain data no other thread can see:
// publish-by-handoff, the same model as command buffers.
//
// Threading contract (owner = the thread that constructed the queue):
//   - Submit, Cancel, DrainCompletions, and PumpWork are owner-thread-only
//     (debug-asserted). Commits may safely Submit follow-up tasks.
//   - work callbacks run on a task thread. They must not touch ambient engine
//     state — only what they captured by value and the data they create.
//   - work and commit must not throw. Debug builds log and abort.
//   - AsyncTaskQueue(0) is the deterministic test mode: Submit only enqueues,
//     PumpWork runs pending work inline on the owner thread (mirror of
//     ThreadPoolJobSystem(0)). PumpWork on a threaded queue asserts.
//   - The destructor joins task threads. Unstarted work and undrained commits
//     are dropped, never half-run.
//=============================================================================
class AsyncTaskQueue
{
public:
    static constexpr std::size_t NoLimit = AsyncDrainBudget::NoLimit;

    explicit AsyncTaskQueue(uint32_t workerCount);
    ~AsyncTaskQueue();

    AsyncTaskQueue(const AsyncTaskQueue&) = delete;
    AsyncTaskQueue& operator=(const AsyncTaskQueue&) = delete;

    [[nodiscard]] uint32_t WorkerCount() const
    {
        return static_cast<uint32_t>(Workers.size());
    }

    // work produces the payload off-thread; commit consumes it on the owner
    // thread during a later DrainCompletions. TPayload needs only move
    // construction (std::unique_ptr payloads are the expected shape).
    template <typename TPayload>
    AsyncTaskHandle Submit(std::function<TPayload()> work,
                           std::function<void(TPayload)> commit)
    {
        assert(work && "AsyncTaskQueue::Submit: work must not be empty");
        assert(commit && "AsyncTaskQueue::Submit: commit must not be empty");
        return SubmitErased(
            [work = std::move(work), commit = std::move(commit)]() -> std::function<void()>
            {
                auto payload = std::make_shared<TPayload>(work());
                return [commit, payload] { commit(std::move(*payload)); };
            });
    }

    [[nodiscard]] AsyncTaskState GetState(const AsyncTaskHandle& handle) const
    {
        assert(handle.IsValid() && "AsyncTaskQueue::GetState: invalid handle");
        return handle.State->load();
    }

    // True once the commit callback has run. Cancelled tasks never complete.
    [[nodiscard]] bool IsComplete(const AsyncTaskHandle& handle) const
    {
        return handle.IsValid() && handle.State->load() == AsyncTaskState::Committed;
    }

    // Best effort: succeeds for Pending (work never runs) and AwaitingCommit
    // (commit never runs). Returns false for Running, Committed, Cancelled.
    bool Cancel(const AsyncTaskHandle& handle);

    // Runs pending commits in completion order on the calling (owner) thread,
    // within the budget (see AsyncDrainBudget for the exact semantics of each
    // axis). Returns the number of commits run. The engine calls this once
    // per frame from FramePhase::DrainAsyncTasks with the time budget from
    // EngineRuntimeConfig::AsyncCommitBudgetMs.
    std::size_t DrainCompletions(const AsyncDrainBudget& budget = {});

    // Zero-worker test mode only: runs up to maxTasks pending work callbacks
    // inline. Returns the number of work callbacks run (cancelled tasks are
    // skipped without counting).
    std::size_t PumpWork(std::size_t maxTasks = NoLimit);

private:
    // Work stage is type-erased as "a closure that returns the commit
    // closure": the payload lives inside the returned closure, so the queue
    // itself never names a payload type.
    using ErasedWork = std::function<std::function<void()>()>;

    struct Task
    {
        std::shared_ptr<std::atomic<AsyncTaskState>> State;
        ErasedWork Work;
        std::function<void()> Commit;
    };

    AsyncTaskHandle SubmitErased(ErasedWork work);

    // Pops one pending task and runs its work stage. Returns false if no
    // pending task existed. Shared by worker threads and PumpWork.
    bool RunOnePendingTask();

    void WorkerMain();

    std::vector<std::thread> Workers;
    mutable std::mutex Mutex;
    std::condition_variable WorkSignal;
    std::deque<Task> PendingTasks;    // guarded by Mutex
    std::deque<Task> CompletedTasks;  // guarded by Mutex
    bool ShutdownRequested = false;   // guarded by Mutex

    // Owner-thread contract backing (checked in debug asserts only).
    std::thread::id OwnerThread;
};
