#pragma once

struct FrameUpdateContext;

// Advances the World's CameraFovScaleState on the presentation clock for the
// active camera, before extraction draws from it. Registered only where a
// window presents.
struct CameraFovScaleSystem
{
    void FrameUpdate(FrameUpdateContext& ctx);
};
