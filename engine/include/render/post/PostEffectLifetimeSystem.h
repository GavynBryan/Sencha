#pragma once

struct FrameUpdateContext;

// Ends post effect contributions on wall time, so they run out while the simulation
// is paused too. Registered only where a window presents.
class PostEffectLifetimeSystem
{
public:
    void FrameUpdate(FrameUpdateContext& ctx);
};
