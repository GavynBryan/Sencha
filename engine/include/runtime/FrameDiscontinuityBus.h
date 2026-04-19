#pragma once

#include <time/FrameClock.h>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

//=============================================================================
// FrameDiscontinuityBus
//
// Broadcast channel for "time just jumped — drop your history" signals.
// RuntimeFrameLoop publishes events when it applies a discontinuity (resize,
// swapchain recreate, restore, teleport, zone load, etc.). Subsystems that
// carry temporal state (presentation transforms, particles, animation
// blending, audio DSP, camera smoothing, network predictor) subscribe and
// snap to the authoritative state.
//
// Subscribers are identified by opaque tokens so unregister is safe during
// destruction. Listener callbacks are invoked synchronously on the runtime
// thread at the point the discontinuity is applied.
//=============================================================================

enum class TemporalDiscontinuityReason;

struct FrameDiscontinuityEvent
{
    TemporalDiscontinuityReason Reason{};
    uint64_t FrameIndex = 0;
};

using FrameDiscontinuityToken = uint64_t;
using FrameDiscontinuityListener = std::function<void(const FrameDiscontinuityEvent&)>;

class FrameDiscontinuityBus
{
public:
    FrameDiscontinuityToken Subscribe(FrameDiscontinuityListener listener)
    {
        const FrameDiscontinuityToken token = ++NextToken;
        Listeners.push_back({ token, std::move(listener) });
        return token;
    }

    void Unsubscribe(FrameDiscontinuityToken token)
    {
        for (auto it = Listeners.begin(); it != Listeners.end(); ++it)
        {
            if (it->Token == token)
            {
                Listeners.erase(it);
                return;
            }
        }
    }

    void Publish(const FrameDiscontinuityEvent& event)
    {
        for (const auto& entry : Listeners)
        {
            if (entry.Callback)
                entry.Callback(event);
        }
    }

    [[nodiscard]] std::size_t ListenerCount() const { return Listeners.size(); }

private:
    struct Entry
    {
        FrameDiscontinuityToken Token;
        FrameDiscontinuityListener Callback;
    };

    std::vector<Entry> Listeners;
    FrameDiscontinuityToken NextToken = 0;
};
