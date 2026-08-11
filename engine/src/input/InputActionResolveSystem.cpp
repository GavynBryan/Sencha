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

void InputActionResolveSystem::ReportBindStatus(World& world, const InputBindStatus& status)
{
    if (status.Revision == ReportedErrorRevision)
        return;
    ReportedErrorRevision = status.Revision;

    const std::string message = DescribeBindErrors(status);

    // Bindings that fail to resolve leave the player without the controls they
    // authored, so this is worth saying out loud rather than leaving to a
    // silent no-op. A partial failure reports here too: dropping one binding
    // keeps the profile working, which is exactly why nothing else would ever
    // notice it happened.
    if (!message.empty() && Log != nullptr)
        Log->Error("input profile: {}", message);
    if (InputActionState* state = world.TryGetResource<InputActionState>())
        state->SetError(message);
}

const BoundInputProfile* InputActionResolveSystem::ResolveProfile(World& world)
{
    InputProfileBinding* binding = world.TryGetResource<InputProfileBinding>();
    if (binding == nullptr || !binding->Profile.IsValid())
        return nullptr;

    InputBindingCache* cache = world.TryGetResource<InputBindingCache>();
    if (cache == nullptr)
        cache = &world.AddResource<InputBindingCache>(*DataAssets);

    const BoundInputProfile* profile = cache->Get(binding->Profile);
    ReportBindStatus(world, cache->Status(binding->Profile));
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
        // Anything held was held under bindings that are gone. Publishing the
        // release they owe is what stops a consumer from reading a held action
        // for the rest of the session after a profile swap fails. The sampled
        // shares go with them, or a consumer integrating a rate would keep
        // turning on a stick reading that no longer has a binding behind it.
        if (InputActionState* state = world.TryGetResource<InputActionState>())
        {
            ReleaseInputActions(Presentation, state->FrameStorage());
            for (InputActionValue& sampled : state->FrameSampledStorage())
                sampled = InputActionValue{};
            for (InputActionValue& sampled : state->TickSampledStorage())
                sampled = InputActionValue{};
        }
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

    ResolveInputActions(*profile, ActiveMask, Devices, Presentation,
                        state->FrameStorage(), 1, state->FrameSampledStorage());
}

void InputActionResolveSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    const BoundInputProfile* profile = ResolveProfile(world);

    InputActionState* state = world.TryGetResource<InputActionState>();
    if (state == nullptr)
        return;

    // Opened before the profile is tested, because a tick that resolves nothing
    // still has to publish something. Leaving the previous record newest would
    // serve it to every tick from here on.
    const std::span<InputActionValue> storage = state->BeginTick(ctx.Time.TickIndex);
    if (storage.empty())
        return;

    if (profile == nullptr)
    {
        ReleaseInputActions(Simulation, storage);
        for (InputActionValue& sampled : state->TickSampledStorage())
            sampled = InputActionValue{};
        return;
    }

    // ActiveMask is whatever PreSimulate settled for this frame: every tick of
    // one frame resolves against the same contexts.
    //
    // The frame's device snapshot, not the live platform frame: simulation is
    // downstream of the pump, and every tick of one frame owes the same answer.
    //
    // Motion is the exception, and takes the tick count for that reason: the
    // frame latched one displacement covering the span these ticks divide
    // between them, so each takes its share rather than the whole thing.
    ResolveInputActions(*profile, ActiveMask, Devices, Simulation, storage,
                        ctx.TicksLeftInFrame, state->TickSampledStorage());
}
