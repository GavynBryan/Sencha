#include <jobs/ThreadPoolJobSystem.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace
{
    // 0 = forking caller, 1..WorkerCount = pool workers. A worker's index is
    // set once at thread start and never changes; the caller's stays 0.
    thread_local uint32_t TlsWorkerIndex = 0;

    // True only while a job callback is running on this thread. Backs the
    // no-nesting assert and the CurrentWorkerIndex validity assert.
    thread_local bool TlsInJob = false;
}

uint32_t ThreadPoolJobSystem::DefaultWorkerCount()
{
    const uint32_t hw = std::thread::hardware_concurrency();
    return hw > 2 ? hw - 2 : 0;
}

ThreadPoolJobSystem::ThreadPoolJobSystem(uint32_t workerCount)
{
    Workers.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i)
    {
        Workers.emplace_back(&ThreadPoolJobSystem::WorkerMain, this, i + 1);
    }
}

ThreadPoolJobSystem::~ThreadPoolJobSystem()
{
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

uint32_t ThreadPoolJobSystem::CurrentWorkerIndex() const
{
    assert(TlsInJob && "CurrentWorkerIndex is only valid inside a job callback");
    return TlsWorkerIndex;
}

void ThreadPoolJobSystem::ParallelFor(uint32_t jobCount,
                                      const std::function<void(uint32_t)>& fn)
{
    assert(!TlsInJob && "ParallelFor must not be called from inside a job (no nesting)");
#ifndef NDEBUG
    const bool wasActive = DebugParallelForActive.exchange(true);
    assert(!wasActive && "ParallelFor is single-active per pool; a second call raced an in-flight one");
#endif

    if (jobCount == 0)
    {
#ifndef NDEBUG
        DebugParallelForActive.store(false);
#endif
        return;
    }

    auto batch = std::make_shared<Batch>();
    batch->Fn = &fn;
    batch->JobCount = jobCount;

    {
        std::lock_guard<std::mutex> lock(Mutex);
        CurrentBatch = batch;
        ++Generation;
    }
    WorkSignal.notify_all();

    ExecuteBatch(*batch);

    if (batch->CompletedCount.load() != jobCount)
    {
        std::unique_lock<std::mutex> lock(Mutex);
        DoneSignal.wait(lock, [&] { return batch->CompletedCount.load() == jobCount; });
    }

#ifndef NDEBUG
    DebugParallelForActive.store(false);
#endif
}

void ThreadPoolJobSystem::WorkerMain(uint32_t workerIndex)
{
    TlsWorkerIndex = workerIndex;

    uint64_t seenGeneration = 0;
    for (;;)
    {
        std::shared_ptr<Batch> batch;
        {
            std::unique_lock<std::mutex> lock(Mutex);
            WorkSignal.wait(lock, [&] {
                return ShutdownRequested || Generation != seenGeneration;
            });
            if (ShutdownRequested)
            {
                return;
            }
            seenGeneration = Generation;
            batch = CurrentBatch;
        }
        ExecuteBatch(*batch);
    }
}

void ThreadPoolJobSystem::ExecuteBatch(Batch& batch)
{
    for (;;)
    {
        const uint32_t index = batch.NextIndex.fetch_add(1);
        if (index >= batch.JobCount)
        {
            break;
        }

        TlsInJob = true;
#ifndef NDEBUG
        try
        {
            (*batch.Fn)(index);
        }
        catch (...)
        {
            std::fprintf(stderr, "ThreadPoolJobSystem: job %u threw; job callbacks must not throw\n", index);
            std::abort();
        }
#else
        (*batch.Fn)(index);
#endif
        TlsInJob = false;

        if (batch.CompletedCount.fetch_add(1) + 1 == batch.JobCount)
        {
            // Lock pairs this notify with the caller's predicate check so the
            // wake cannot slip between the caller's check and its wait.
            std::lock_guard<std::mutex> lock(Mutex);
            DoneSignal.notify_all();
        }
    }
}
