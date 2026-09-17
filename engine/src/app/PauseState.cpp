#include <app/PauseState.h>

#include <input/InputContextSet.h>
#include <runtime/RuntimeFrameLoop.h>

void PauseState::Request(PausePhase phase)
{
    Pending = phase;
}

PauseTransition PauseState::Apply(RuntimeFrameLoop& runtime, InputContextSet& contexts)
{
    if (Pending == Current)
        return PauseTransition::None;

    Current = Pending;

    if (Current == PausePhase::Paused)
    {
        // Gameplay controls go quiet by suspending the context set, not by
        // taking anyone's lease and not by out-prioritising them. A context
        // claims only the controls it binds, so a menu context above gameplay
        // would leave movement and firing resolving; and the leases belong to
        // whoever took them, who gets them back untouched.
        contexts.SetSuspended(true);

        if (Effective() == PausePolicy::SuspendSimulation)
        {
            // Suspension, never the timescale. The rate is somebody else's
            // fact, and overwriting it here would mean restoring it later --
            // which silently reverts a rate legitimately changed while paused.
            runtime.SetSimulationSuspended(true);
            SuspendedSimulation = true;
        }
        return PauseTransition::Entered;
    }

    contexts.SetSuspended(false);
    if (SuspendedSimulation)
    {
        runtime.SetSimulationSuspended(false);
        SuspendedSimulation = false;

        // Simulated time did not pass while suspended, so the input the
        // simulation latched in the meantime describes a world it never saw:
        // a menu's worth of pointer travel, and whatever was clicked in it.
        // This is what tells the mapper to drop it, and it drops the scheduler
        // residual so resuming cannot replay a partial tick either.
        runtime.MarkTemporalDiscontinuity(TemporalDiscontinuityReason::SimulationPause);
    }
    return PauseTransition::Left;
}
