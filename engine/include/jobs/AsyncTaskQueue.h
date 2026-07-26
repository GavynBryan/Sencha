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
    Pending,
    Running,
    AwaitingCommit,
    Committed,
    Cancelled,
};

class AsyncTaskHandle
{
public:
    AsyncTaskHandle() = default;
    [[nodiscard]] bool IsValid() const { return State != nullptr; }

private:
    friend class AsyncTaskQueue;
    explicit AsyncTaskHandle(
        std::shared_ptr<std::atomic<AsyncTaskState>> state)
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
//   MaxCommits — hard cap. 0 is valid and drains nothing.
//
//   MaxTime — soft wall-time cap for frame pacing. The first ready commit
//   always runs, and no further commit starts once elapsed time exceeds the
//   budget. A budget cannot split a commit, so large payloads must be shaped as
//   multiple chunked tasks. The budget then meters between chunks.
//=============================================================================
struct AsyncDrainBudget
{
    static constexpr std::size_t NoLimit =
        static_cast<std::size_t>(-1);

    std::size_t MaxCommits = NoLimit;
    std::chrono::steady_clock::duration MaxTime =
        std::chrono::steady_clock::duration::max();
};

//=============================================================================
// AsyncTaskQueue
//
// Async cross-frame lane. Long-running IO, decode, and detached package builds
// run on dedicated task threads; results re-enter engine state only through
// owner-thread commit callbacks at the frame drain point. Until commit runs,
// payloads are plain data no other thread can observe.
//
// Threading contract:
//   - Submit, Cancel, DrainCompletions, and PumpWork are owner-thread-only.
//   - work callbacks must not touch ambient engine state.
//   - work and commit callbacks must not throw.
//   - AsyncTaskQueue(0) is deterministic test mode; PumpWork is illegal when
//     worker threads exist.
//   - destruction joins workers and drops unstarted or undrained work.
//=============================================================================
class AsyncTaskQueue
{
public:
    static constexpr std::size_t NoLimit =
        AsyncDrainBudget::NoLimit;

    explicit AsyncTaskQueue(uint32_t workerCount);
    ~AsyncTaskQueue();

    AsyncTaskQueue(const AsyncTaskQueue&) = delete;
    AsyncTaskQueue& operator=(const AsyncTaskQueue&) = delete;

    [[nodiscard]] uint32_t WorkerCount() const
    {
        return static_cast<uint32_t>(Workers.size());
    }

    template <typename TPayload>
    AsyncTaskHandle Submit(
        std::function<TPayload()> work,
        std::function<void(TPayload)> commit)
    {
        assert(work
               && "AsyncTaskQueue::Submit: work must not be empty");
        assert(commit
               && "AsyncTaskQueue::Submit: commit must not be empty");
        return SubmitErased(
            [work = std::move(work),
             commit = std::move(commit)]()
                -> std::function<void()>
            {
                auto payload = std::make_shared<TPayload>(work());
                return [commit, payload]
                {
                    commit(std::move(*payload));
                };
            });
    }

    [[nodiscard]] AsyncTaskState GetState(
        const AsyncTaskHandle& handle) const
    {
        assert(handle.IsValid()
               && "AsyncTaskQueue::GetState: invalid handle");
        return handle.State->load();
    }

    [[nodiscard]] bool IsComplete(
        const AsyncTaskHandle& handle) const
    {
        return handle.IsValid()
            && handle.State->load()
                == AsyncTaskState::Committed;
    }

    bool Cancel(const AsyncTaskHandle& handle);

    std::size_t DrainCompletions(
        const AsyncDrainBudget& budget = {});

    std::size_t PumpWork(
        std::size_t maxTasks = NoLimit);

private:
    using ErasedWork =
        std::function<std::function<void()>()>;

    struct Task
    {
        std::shared_ptr<std::atomic<AsyncTaskState>> State;
        ErasedWork Work;
        std::function<void()> Commit;
    };

    AsyncTaskHandle SubmitErased(ErasedWork work);
    bool RunOnePendingTask();
    void WorkerMain();

    std::vector<std::thread> Workers;
    mutable std::mutex Mutex;
    std::condition_variable WorkSignal;
    std::deque<Task> PendingTasks;
    std::deque<Task> CompletedTasks;
    bool ShutdownRequested = false;
    std::thread::id OwnerThread;
};
