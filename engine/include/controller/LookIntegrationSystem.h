#pragma once

#include <app/GameContexts.h>

//=============================================================================
// LookIntegrationSystem
//
// Turns per-frame look displacement into accumulated orientation.
//
// Runs on the presentation clock, in PreSimulate. Aiming has to track the rate
// frames arrive or it visibly steps: at a 144 Hz display and a 60 Hz simulation,
// integrating only on ticks would move the view 60 times a second. PreSimulate
// rather than FrameUpdate because simulation steers along this orientation
// during the tick, and accumulating afterwards would aim every tick at where the
// player was looking on the previous frame.
//
// The consequence is that simulation reads a frame-clocked value, which is
// correct for local play and is the thing a replayable player command has to
// replace: a command carries the orientation sampled for its tick, and replay
// feeds that back rather than re-reading this component.
//=============================================================================
class LookIntegrationSystem
{
public:
    void PreSimulate(PreSimulateContext& ctx);
};
