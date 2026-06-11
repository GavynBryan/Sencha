#pragma once

#include <jobs/JobSystem.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

//=============================================================================
// ThreadPoolJobSystem
//
// Fixed-size worker pool behind the JobSystem interface. Workers park on a
// condition variable between batches; ParallelFor publishes one batch, wakes
// the pool, and the caller executes jobs from the same atomic counter until
// the batch is exhausted, then waits for stragglers.
//
// Batch state lives in a shared_ptr so a worker that wakes late — after its
// batch already completed — sees an exhausted counter on its own copy and
// goes back to sleep without ever touching state from a newer batch.
//
// ThreadPoolJobSystem(0) runs everything inline on the calling thread in
// index order: the deterministic configuration for tests.
//=============================================================================
class ThreadPoolJobSystem final : public JobSystem
{
public:
    explicit ThreadPoolJobSystem(uint32_t workerCount);
    ~ThreadPoolJobSystem() override;

    // hardware_concurrency() - 2 (main thread and render path are already
    // occupied), clamped to 0 — which degrades to caller-only execution on
    // small machines rather than oversubscribing them.
    [[nodiscard]] static uint32_t DefaultWorkerCount();

    ThreadPoolJobSystem(const ThreadPoolJobSystem&) = delete;
    ThreadPoolJobSystem& operator=(const ThreadPoolJobSystem&) = delete;

    uint32_t WorkerCount() const override
    {
        return static_cast<uint32_t>(Workers.size());
    }

    uint32_t CurrentWorkerIndex() const override;

    void ParallelFor(uint32_t jobCount,
                     const std::function<void(uint32_t jobIndex)>& fn) override;

private:
    struct Batch
    {
        const std::function<void(uint32_t)>* Fn = nullptr;
        uint32_t JobCount = 0;
        std::atomic<uint32_t> NextIndex{ 0 };
        std::atomic<uint32_t> CompletedCount{ 0 };
    };

    void WorkerMain(uint32_t workerIndex);

    // Caller-and-worker shared execution loop: pull indices until exhausted.
    void ExecuteBatch(Batch& batch);

    std::vector<std::thread> Workers;
    std::mutex Mutex;
    std::condition_variable WorkSignal;   // workers: a new batch is published
    std::condition_variable DoneSignal;   // caller: last job of a batch done
    std::shared_ptr<Batch> CurrentBatch;  // guarded by Mutex
    uint64_t Generation = 0;              // guarded by Mutex
    bool ShutdownRequested = false;       // guarded by Mutex

#ifndef NDEBUG
    std::atomic<bool> DebugParallelForActive{ false };
#endif
};
