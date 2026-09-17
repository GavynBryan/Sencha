#pragma once

#include <app/GameContexts.h>
#include <input/InputAction.h>

class BackRouter;
class PauseState;

//=============================================================================
// PauseInputSystem
//
// The one reader of the shell's Back action, and the one place a frame learns
// that it is not going to simulate after all.
//
// An ECS system because reading a mapped action means reading InputActionState
// out of the World on the right clock, and the schedule is how anything does
// that. Ordered after the resolve system, which both share a phase with.
//
// It runs late in FramePhase::ScheduleTicks -- after the tick budget is
// computed, before the tick loop -- which is the one window in which a frame
// still has ticks to cancel. That is why recognising a pause here stops the
// frame that recognised it, rather than letting an already-budgeted batch run.
//=============================================================================
class PauseInputSystem
{
public:
    PauseInputSystem(PauseState& pause, BackRouter& router);

    void PreSimulate(PreSimulateContext& ctx);

private:
    PauseState* Pause = nullptr;
    BackRouter* Router = nullptr;

    // Resolved once the profile has bound, then indexed. The shell's actions
    // take fixed dense ids in every profile, so this is a constant rather than
    // a name looked up per frame -- but it is still resolved rather than
    // assumed, because a world with no input resources at all has none.
    InputActionId BackAction;
};
