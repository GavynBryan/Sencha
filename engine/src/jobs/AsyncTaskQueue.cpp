#include <jobs/AsyncTaskQueue.h>

#include <cstdio>
#include <cstdlib>

namespace
{
    // Debug wrapper enforcing the no-throw contract for work and commit.
    template <typename Fn, typename... Args>
    auto RunGuarded(const char* stage, Fn&& fn, Args&&... args)
    {
#ifndef NDEBUG
        try
        {
            return fn(std::forward<Args>(args)...);
        }
        catch (...)
        {
            std::fprintf(stderr,
                         "AsyncTaskQueue: %s callback threw; task callbacks must not throw\n",
                         stage);
            std::abort();
        }
#else
        return fn(std::forward<Args>(args)...);
#endif
    }
}

AsyncTaskQueue::AsyncTaskQueue(uint32_t workerCount)
    : OwnerThread(std::this_thread::get_id())
{
    Workers.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i)
    {
        Workers.emplace_back(&AsyncTaskQueue::WorkerMain, this);
    }
}

AsyncTaskQueue::~AsyncTaskQueue()
{
    assert(std::this_thread::get_id() == OwnerThread
           && "AsyncTaskQueue must be destroyed on its owner thread");
    {
        std::lock_guard<std::mutex> lock(Mutex);
        ShutdownRequested = true;
    }
    WorkSignal.notify_all();
    for (auto& worker : Workers)
    {
        worker.join();
    }
}

AsyncTaskHandle AsyncTaskQueue::SubmitErased(ErasedWork work)
{
    assert(std::this_thread::get_id() == OwnerThread
           && "AsyncTaskQueue::Submit is owner-thread-only");

    auto state = std::make_shared<std::atomic<AsyncTaskState>>(AsyncTaskState::Pending);

    {
        std::lock_guard<std::mutex> lock(Mutex);
        PendingTasks.push_back(Task{ state, std::move(work), {} });
    }
    if (!Workers.empty())
    {
        WorkSignal.notify_one();
    }
    return AsyncTaskHandle{ std::move(state) };
}

bool AsyncTaskQueue::Cancel(const AsyncTaskHandle& handle)
{
    assert(std::this_thread::get_id() == OwnerThread
           && "AsyncTaskQueue::Cancel is owner-thread-only");
    assert(handle.IsValid() && "AsyncTaskQueue::Cancel: invalid handle");

    AsyncTaskState expected = handle.State->load();
    while (expected == AsyncTaskState::Pending || expected == AsyncTaskState::AwaitingCommit)
    {
        if (handle.State->compare_exchange_weak(expected, AsyncTaskState::Cancelled))
        {
            return true;
        }
    }
    return false;
}

std::size_t AsyncTaskQueue::DrainCompletions(const AsyncDrainBudget& budget)
{
    assert(std::this_thread::get_id() == OwnerThread
           && "AsyncTaskQueue::DrainCompletions is owner-thread-only");

    // Pop one, commit one: the time check has to sit between commits, and
    // commits run unlocked because they may Submit follow-up tasks while task
    // threads keep publishing completions.
    const auto start = std::chrono::steady_clock::now();
    std::size_t committed = 0;

    while (committed < budget.MaxCommits)
    {
        // MaxTime is soft: the first commit always runs, later ones only
        // while the budget has time left (AsyncDrainBudget contract).
        if (committed > 0 && std::chrono::steady_clock::now() - start >= budget.MaxTime)
        {
            break;
        }

        Task task;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            if (CompletedTasks.empty())
            {
                break;
            }
            task = std::move(CompletedTasks.front());
            CompletedTasks.pop_front();
        }

        AsyncTaskState expected = AsyncTaskState::AwaitingCommit;
        if (!task.State->compare_exchange_strong(expected, AsyncTaskState::Committed))
        {
            continue;  // cancelled between completion and drain: drop, free budget
        }
        RunGuarded("commit", task.Commit);
        ++committed;
    }
    return committed;
}

std::size_t AsyncTaskQueue::PumpWork(std::size_t maxTasks)
{
    assert(std::this_thread::get_id() == OwnerThread
           && "AsyncTaskQueue::PumpWork is owner-thread-only");
    assert(Workers.empty()
           && "AsyncTaskQueue::PumpWork is for the zero-worker test mode only");

    std::size_t ran = 0;
    while (ran < maxTasks && RunOnePendingTask())
    {
        ++ran;
    }
    return ran;
}

bool AsyncTaskQueue::RunOnePendingTask()
{
    Task task;
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(Mutex);
            if (PendingTasks.empty())
            {
                return false;
            }
            task = std::move(PendingTasks.front());
            PendingTasks.pop_front();
        }

        AsyncTaskState expected = AsyncTaskState::Pending;
        if (task.State->compare_exchange_strong(expected, AsyncTaskState::Running))
        {
            break;  // claimed; run it
        }
        // Cancelled while pending: discard (payload-free) and try the next.
    }

    task.Commit = RunGuarded("work", task.Work);
    task.Work = nullptr;
    task.State->store(AsyncTaskState::AwaitingCommit);

    {
        std::lock_guard<std::mutex> lock(Mutex);
        CompletedTasks.push_back(std::move(task));
    }
    return true;
}

void AsyncTaskQueue::WorkerMain()
{
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(Mutex);
            WorkSignal.wait(lock, [&] { return ShutdownRequested || !PendingTasks.empty(); });
            if (ShutdownRequested)
            {
                return;
            }
        }
        RunOnePendingTask();
    }
}
