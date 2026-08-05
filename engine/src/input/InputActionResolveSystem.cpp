#include <input/InputActionResolveSystem.h>

#include <assets/data/DataAssetCache.h>
#include <core/logging/LoggingProvider.h>
#include <input/InputActionState.h>
#include <input/InputContextSet.h>
#include <input/InputRegistration.h>

InputActionResolveSystem::InputActionResolveSystem(DataAssetCache& dataAssets,
                                                   LoggingProvider& logging)
    : DataAssets(&dataAssets)
    , Log(&logging.GetLogger<InputActionResolveSystem>())
{
}

const BoundInputProfile* InputActionResolveSystem::ResolveProfile(World& world)
{
    InputProfileBinding* binding = world.TryGetResource<InputProfileBinding>();
    if (binding == nullptr || !binding->Profile.IsValid())
        return nullptr;

    InputBindingCache* cache = world.TryGetResource<InputBindingCache>();
    if (cache == nullptr)
        cache = &world.AddResource<InputBindingCache>(*DataAssets);

    std::string error;
    const BoundInputProfile* profile = cache->Get(binding->Profile, &error);
    if (!error.empty())
    {
        // Bindings that fail to resolve leave the player without the controls
        // they authored, so this is worth saying out loud rather than leaving
        // to a silent no-op.
        if (Log != nullptr)
            Log->Error("input profile: {}", error);
        if (InputActionState* state = world.TryGetResource<InputActionState>())
            state->SetError(error);
    }
    return profile;
}

void InputActionResolveSystem::PreSimulate(PreSimulateContext& ctx)
{
    World& world = ctx.Entities;
    const BoundInputProfile* profile = ResolveProfile(world);

    // Both clocks still take the frame's transitions when no profile is bound,
    // so a profile loaded mid-session does not inherit a backlog of stale
    // impulses on its first pass.
    AccumulateInputFrame(ctx.Input, Devices, Presentation, Simulation);

    if (profile == nullptr)
    {
        Presentation.Latch.Clear();
        Simulation.Latch.Clear();
        return;
    }

    InputActionState* state = world.TryGetResource<InputActionState>();
    if (state == nullptr)
        state = &world.AddResource<InputActionState>();
    state->Configure(profile->ActionCount());

    InputContextSet* contexts = world.TryGetResource<InputContextSet>();
    if (contexts == nullptr)
        contexts = &world.AddResource<InputContextSet>();

    contexts->ApplyPending();
    contexts->BuildActiveMask(*profile, ActiveMask);

    ResolveInputActions(*profile, ActiveMask, Devices, Presentation, state->FrameStorage());
}

void InputActionResolveSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    const BoundInputProfile* profile = ResolveProfile(world);
    if (profile == nullptr)
        return;

    InputActionState* state = world.TryGetResource<InputActionState>();
    if (state == nullptr)
        return;

    // ActiveMask is whatever PreSimulate settled for this frame: every tick of
    // one frame resolves against the same contexts.
    const std::span<InputActionValue> storage = state->BeginTick(ctx.Time.TickIndex);
    if (storage.empty())
        return;

    // The frame's device snapshot, not the live platform frame: simulation is
    // downstream of the pump, and every tick of one frame owes the same answer.
    ResolveInputActions(*profile, ActiveMask, Devices, Simulation, storage);
}
