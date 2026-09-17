#include <app/PauseInputSystem.h>

#include <app/BackRouter.h>
#include <app/PauseState.h>
#include <ecs/World.h>
#include <input/InputActionRegistry.h>
#include <input/InputActionState.h>
#include <input/InputContextSet.h>
#include <input/ShellInputActions.h>
#include <runtime/RuntimeFrameLoop.h>

PauseInputSystem::PauseInputSystem(PauseState& pause, BackRouter& router)
    : Pause(&pause)
    , Router(&router)
    , BackAction(InputActionRegistry::IdAt(static_cast<std::size_t>(ShellAction::Back)))
{
}

void PauseInputSystem::PreSimulate(PreSimulateContext& ctx)
{
    InputContextSet* contexts = ctx.Entities.TryGetResource<InputContextSet>();
    if (contexts == nullptr)
        return;

    const InputActionState* actions = ctx.Entities.TryGetResource<InputActionState>();
    if (actions != nullptr && actions->Frame().Fired(BackAction))
    {
        // Offered, not claimed. A console, a text field, a game's own screen
        // may each want it first; opening the shell is what happens when none
        // of them does.
        (void)Router->Dispatch();
    }

    // Only a transition into the paused state cancels the frame's simulation.
    // Back is not a synonym for pause: closing an inventory with it must not
    // turn the frame into a zero-tick frame.
    if (Pause->Apply(ctx.Runtime, *contexts) == PauseTransition::Entered)
        ctx.Runtime.CancelFixedTicksThisFrame();
}
