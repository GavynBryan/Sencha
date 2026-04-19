#pragma once

#include <platform/WindowTypes.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

//=============================================================================
// SwapchainRebuildWorker
//
// Runs non-render-thread rebuild preparation on a dedicated worker thread.
// The callback must not mutate Vulkan objects that are also owned or read by
// the main/render thread. Actual swapchain recreation belongs on the graphics
// owner thread unless the renderer has an explicit synchronization model.
//
// Threading model:
//   - main loop calls Request() once per dirty swapchain.
//   - worker picks up, runs callback, publishes result.
//   - main loop calls Poll() each frame; returns Ready once, then Idle.
//=============================================================================
class SwapchainRebuildWorker
{
public:
    enum class PollResult
    {
        Idle,      // No rebuild requested or in flight.
        InFlight,  // Worker is busy; keep producing lifecycle-only frames.
        Ready,     // Last rebuild completed successfully.
        Failed,    // Last rebuild failed; caller should mark surface invalid.
    };

    using Callback = std::function<bool(WindowExtent)>;

    SwapchainRebuildWorker();
    ~SwapchainRebuildWorker();

    SwapchainRebuildWorker(const SwapchainRebuildWorker&) = delete;
    SwapchainRebuildWorker& operator=(const SwapchainRebuildWorker&) = delete;

    void Start(Callback callback);
    void Stop();

    // Request a rebuild to the given extent. Coalesces queued work and records
    // a follow-up request if a rebuild is already running.
    bool Request(WindowExtent extent);

    // Poll from the main loop. Consumes Ready/Failed states on first read.
    PollResult Poll(WindowExtent* completedExtent = nullptr);

    [[nodiscard]] bool IsBusy() const
    {
        const StateValue state = State.load();
        return state == StateValue::Pending || state == StateValue::Running;
    }

private:
    enum class StateValue
    {
        Idle,
        Pending,
        Running,
        Ready,
        Failed,
    };

    void WorkerMain();

    Callback RebuildCallback;
    std::thread Worker;
    std::mutex Mutex;
    std::condition_variable Signal;
    std::atomic<StateValue> State{ StateValue::Idle };
    std::atomic<bool> ShutdownRequested{ false };
    WindowExtent PendingExtent{};
    WindowExtent CompletedExtent{};
    bool QueuedExtentPending = false;
};
