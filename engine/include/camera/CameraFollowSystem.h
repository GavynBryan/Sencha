#pragma once

struct FrameUpdateContext;

//=============================================================================
// CameraFollowSystem
//
// Places the active camera from its CameraRig each rendered frame, following its
// target and applying look input. This is an engine schedule adapter; the rig and
// pose math remain backend-free framework data.
//=============================================================================
class CameraFollowSystem
{
public:
    void FrameUpdate(FrameUpdateContext& ctx);
};
