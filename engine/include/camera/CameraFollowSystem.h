#pragma once

struct FrameUpdateContext;
struct PreSimulateContext;

//=============================================================================
// CameraFollowSystem
//
// Places the active camera from its CameraRig each rendered frame, following its
// target and applying look input. This is an engine schedule adapter; the rig and
// pose math remain backend-free framework data.
//
// The work splits across two phases because its two halves answer to different
// clocks. Look accumulation runs before simulation, since the tick steers on the
// orientation it produces. Placement runs after, because it follows the pose the
// target is being drawn at, which only exists once simulation has advanced.
//=============================================================================
class CameraFollowSystem
{
public:
    void PreSimulate(PreSimulateContext& ctx);
    void FrameUpdate(FrameUpdateContext& ctx);
};
