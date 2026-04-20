#include <runtime/SwapchainRebuildWorker.h>

SwapchainRebuildWorker::SwapchainRebuildWorker() = default;

SwapchainRebuildWorker::~SwapchainRebuildWorker()
{
    Stop();
}

void SwapchainRebuildWorker::Start(Callback callback)
{
    if (Worker.joinable())
        return;
    RebuildCallback = std::move(callback);
    ShutdownRequested.store(false);
    State.store(StateValue::Idle);
    Worker = std::thread(&SwapchainRebuildWorker::WorkerMain, this);
}

void SwapchainRebuildWorker::Stop()
{
    if (!Worker.joinable())
        return;

    {
        std::lock_guard<std::mutex> lock(Mutex);
        ShutdownRequested.store(true);
    }
    Signal.notify_all();
    Worker.join();
    RebuildCallback = nullptr;
}

bool SwapchainRebuildWorker::Request(WindowExtent extent)
{
    if (extent.Width == 0 || extent.Height == 0)
        return false;

    {
        std::lock_guard<std::mutex> lock(Mutex);
        PendingExtent = extent;
        const StateValue current = State.load();
        if (current == StateValue::Running)
        {
            QueuedExtentPending = true;
            return true;
        }
        State.store(StateValue::Pending);
    }
    Signal.notify_one();
    return true;
}

SwapchainRebuildWorker::PollResult SwapchainRebuildWorker::Poll(WindowExtent* completedExtent)
{
    const StateValue current = State.load();
    switch (current)
    {
    case StateValue::Idle:
        return PollResult::Idle;
    case StateValue::Pending:
    case StateValue::Running:
        return PollResult::InFlight;
    case StateValue::Ready:
    {
        bool startQueuedRequest = false;
        std::lock_guard<std::mutex> lock(Mutex);
        if (State.load() == StateValue::Ready)
        {
            if (completedExtent != nullptr)
                *completedExtent = CompletedExtent;
            if (QueuedExtentPending)
            {
                QueuedExtentPending = false;
                State.store(StateValue::Pending);
                startQueuedRequest = true;
            }
            else
            {
                State.store(StateValue::Idle);
            }
        }
        if (startQueuedRequest)
            Signal.notify_one();
        return PollResult::Ready;
    }
    case StateValue::Failed:
    {
        bool startQueuedRequest = false;
        std::lock_guard<std::mutex> lock(Mutex);
        if (State.load() == StateValue::Failed)
        {
            if (completedExtent != nullptr)
                *completedExtent = CompletedExtent;
            if (QueuedExtentPending)
            {
                QueuedExtentPending = false;
                State.store(StateValue::Pending);
                startQueuedRequest = true;
            }
            else
            {
                State.store(StateValue::Idle);
            }
        }
        if (startQueuedRequest)
            Signal.notify_one();
        return PollResult::Failed;
    }
    }
    return PollResult::Idle;
}

void SwapchainRebuildWorker::WorkerMain()
{
    for (;;)
    {
        WindowExtent extent{};
        {
            std::unique_lock<std::mutex> lock(Mutex);
            Signal.wait(lock, [this] {
                return ShutdownRequested.load()
                    || State.load() == StateValue::Pending;
            });

            if (ShutdownRequested.load())
                return;

            extent = PendingExtent;
            State.store(StateValue::Running);
        }

        bool ok = false;
        if (RebuildCallback)
            ok = RebuildCallback(extent);

        {
            std::lock_guard<std::mutex> lock(Mutex);
            CompletedExtent = extent;
            State.store(ok ? StateValue::Ready : StateValue::Failed);
        }
    }
}
