#include <input/InputActionResolveSystem.h>

#include <assets/data/DataAssetCache.h>
#include <core/logging/LoggingProvider.h>
#include <input/InputActionState.h>
#include <input/InputContextSet.h>
#include <input/InputRegistration.h>
#include <input/ShellInputActions.h>
#include <runtime/FrameDiscontinuityBus.h>
#include <runtime/RuntimeFrameLoop.h>

InputActionResolveSystem::InputActionResolveSystem(DataAssetCache& dataAssets,
                                                   LoggingProvider& logging,
                                                   FrameDiscontinuityBus* discontinuities)
    : DataAssets(&dataAssets)
    , Log(&logging.GetLogger<InputActionResolveSystem>())
    , Discontinuities(discontinuities)
{
    if (Discontinuities == nullptr)
        return;

    // Only one reason is handled, and deliberately only one.
    //
    // While simulated time is suspended no fixed tick runs, so nothing drains
    // the simulation latch -- but PreSimulate keeps folding every frame's
    // transitions and motion into it. A minute spent in a menu therefore
    // accumulates a minute of pointer travel and every button edge, including
    // the click that pressed Resume: the event was folded into the snapshot
    // before any surface was offered it, which is the router's contract, so a
    // surface claiming it does not keep it out of here. The first tick after
    // resuming would take the lot.
    //
    // The presentation latch is left alone: it drains every frame, and a world
    // event has no business swallowing a UI click or a half-finished menu
    // navigation. Every other discontinuity reason keeps the behaviour it had;
    // whether a teleport or a zone load should discard simulation input the
    // same way is a separate question with its own reproduction.
    DiscontinuityToken = Discontinuities->Subscribe(
        [this](const FrameDiscontinuityEvent& event) {
            if (event.Reason == TemporalDiscontinuityReason::SimulationPause)
                Simulation.Latch.Clear();
        });
}

InputActionResolveSystem::~InputActionResolveSystem()
{
    if (Discontinuities != nullptr && DiscontinuityToken != 0)
        Discontinuities->Unsubscribe(DiscontinuityToken);
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

const BoundInputProfile* InputActionResolveSystem::ShellOnly()
{
    // A game with no input content is still an application, and backing out of
    // gameplay is the application's operation rather than the game's. So a world
    // that names no profile at all resolves the shell's actions instead of
    // nothing.
    //
    // Only that case. A profile that exists and failed to bind is different:
    // see ResolveProfile.
    if (!ShellOnlyBuilt)
    {
        BuildShellOnlyProfile(ShellOnlyActions, ShellOnlyProfile);
        ShellOnlyBuilt = true;
    }
    return &ShellOnlyProfile;
}

const BoundInputProfile* InputActionResolveSystem::ResolveProfile(World& world)
{
    InputProfileBinding* binding = world.TryGetResource<InputProfileBinding>();
    if (binding == nullptr || !binding->Profile.IsValid())
        return ShellOnly();

    InputBindingCache* cache = world.TryGetResource<InputBindingCache>();
    if (cache == nullptr)
        cache = &world.AddResource<InputBindingCache>(*DataAssets);

    const BoundInputProfile* profile = cache->Get(binding->Profile);
    ReportBindStatus(world, cache->Status(binding->Profile));

    // Deliberately not the shell-only tables. A profile that named an asset and
    // could not bind still defined the id space a game resolved its names
    // against; serving a six-action shell vocabulary in its place would silently
    // point those ids at other actions. Null is what publishes the release every
    // held action owes, which is the behaviour a lost profile has to have.
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
