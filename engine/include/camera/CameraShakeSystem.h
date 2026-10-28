#pragma once

struct FrameUpdateContext;

// Advances the World's CameraShakeState on the presentation clock and sums it
// for the active camera, before extraction draws from it. Registered only
// where a window presents.
struct CameraShakeSystem
{
    void FrameUpdate(FrameUpdateContext& ctx);
};
