#pragma once

#include <app/GameContexts.h>

//=============================================================================
// LookIntegrationSystem
//
// Turns look displacement into accumulated orientation, on both clocks.
//
// FixedLogic integrates the aim simulation steers along. It has to be the tick
// clock: a frame that runs no tick would otherwise turn the heading while no
// simulated time passed, and a frame that runs two would turn it once and then
// step twice under the same angle. Movement is rebuilt per tick from this
// orientation, so a heading that does not advance with simulated time makes the
// velocity direction lurch -- visible whenever the display rate and the tick
// rate are close enough for the tick count per frame to keep changing.
//
// PreSimulate accumulates what the ticks have not consumed yet into
// PendingLookInput, which presentation adds on top of this orientation. Aiming
// has to track the rate frames arrive or it visibly steps: at a 144 Hz display
// and a 60 Hz simulation, a view that only moved on ticks would move 60 times a
// second.
//
// Ordering: after the input mapper on both clocks, and before anything that
// steers along the result during the tick.
//=============================================================================
class LookIntegrationSystem
{
public:
    void PreSimulate(PreSimulateContext& ctx);
    void FixedLogic(FixedLogicContext& ctx);
};
