#pragma once

struct FrameUpdateContext;

//=============================================================================
// CameraFollowSystem
//
// Places the active camera from its CameraRig each rendered frame, following its
// target and presenting the orientation that target is aiming with. This is an
// engine schedule adapter; the rig and pose math remain backend-free framework
// data.
//
// Placement runs after simulation because it follows the pose the target is
// being drawn at, which only exists once simulation has advanced. The camera
// does not accumulate look: LookIntegrationSystem owns the target's orientation,
// and does it before the tick, since simulation steers along it.
//=============================================================================
class CameraFollowSystem
{
public:
    void FrameUpdate(FrameUpdateContext& ctx);
};
