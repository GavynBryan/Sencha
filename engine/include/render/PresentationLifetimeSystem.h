#pragma once

struct FrameUpdateContext;

// Ends presentation contributions once a frame, on wall time so they run out while
// the simulation is paused too: mesh overrides and outlines whose holder has gone,
// outlines whose entity has gone, and post effects held by a holder that has gone
// or whose session has run out. Registered only where a window presents.
class PresentationLifetimeSystem
{
public:
    void FrameUpdate(FrameUpdateContext& ctx);
};
