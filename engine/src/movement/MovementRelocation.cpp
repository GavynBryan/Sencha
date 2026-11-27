#include <movement/MovementRegistration.h>

#include <movement/LocomotionMode.h>
#include <movement/components/HeldMotion.h>
#include <world/relocation/RelocationRules.h>

#include <utility>

namespace
{
    void TurnKinematicState(KinematicState& state, const RelocationFrame& frame)
    {
        state.Velocity = frame.MapVector(state.Velocity);
    }

    void TurnWishDir(MovementIntent& intent, const RelocationFrame& frame)
    {
        intent.WishDir = frame.MapVector(intent.WishDir);
    }

    void TurnBursts(MotionBursts& bursts, const RelocationFrame& frame)
    {
        for (MotionBurst& burst : bursts.Slots)
            burst.Velocity = frame.MapVector(burst.Velocity);
    }

    void TurnImpulse(MotionImpulse& impulse, const RelocationFrame& frame)
    {
        impulse.DeltaVelocity = frame.MapVector(impulse.DeltaVelocity);
    }

    void TurnAxisOverride(MotionAxisOverride& overrides, const RelocationFrame& frame)
    {
        overrides.PlanarVelocity = frame.MapVector(overrides.PlanarVelocity);
    }

    void TurnLocomotionOutput(LocomotionOutput& output, const RelocationFrame& frame)
    {
        output.Velocity = frame.MapVector(output.Velocity);
        output.UpAxis = frame.MapVector(output.UpAxis);
    }

    void TurnMotionRequest(MotionRequest& request, const RelocationFrame& frame)
    {
        request.Velocity = frame.MapVector(request.Velocity);
        request.UpAxis = frame.MapVector(request.UpAxis);
    }

    // Found again by the first sweep on the far side.
    void ForgetSupport(SupportState& support, const RelocationFrame&) { support = SupportState{}; }
    void ForgetImmersion(Immersion& immersion, const RelocationFrame&) { immersion = Immersion{}; }

    // Letting go of the slot is the tether's own end (MovementOperations::EndTether).
    void EndTether(TetherSession& tether, const RelocationFrame&) { tether = TetherSession{}; }

    // A cling holds a surface on the side it left; the mode's own exit ends it at the next transition.
    void LeaveClingMode(void*, World& world, EntityId entity, const RelocationFrame&)
    {
        const LocomotionModeRegistry* modes = std::as_const(world).TryGetResource<LocomotionModeRegistry>();
        ModeTransitionRequest* request = world.TryGet<ModeTransitionRequest>(entity);
        if (modes == nullptr || request == nullptr)
            return;
        *request = ModeTransitionRequest{ .Target = modes->FreeMode(), .Class = ModeRequestClass::Forced, .Pending = true };
    }
}

void RegisterMovementRelocation(World& world)
{
    SetRelocationRule<KinematicState>(world, RelocationRuleKind::Maps,
                                      &RelocateValue<KinematicState, &TurnKinematicState>);
    SetRelocationRule<MovementIntent>(world, RelocationRuleKind::Maps, &RelocateValue<MovementIntent, &TurnWishDir>);
    SetRelocationRule<MotionBursts>(world, RelocationRuleKind::Maps, &RelocateValue<MotionBursts, &TurnBursts>);
    SetRelocationRule<MotionImpulse>(world, RelocationRuleKind::Maps, &RelocateValue<MotionImpulse, &TurnImpulse>);
    SetRelocationRule<MotionAxisOverride>(world, RelocationRuleKind::Maps,
                                          &RelocateValue<MotionAxisOverride, &TurnAxisOverride>);
    SetRelocationRule<LocomotionOutput>(world, RelocationRuleKind::Maps,
                                        &RelocateValue<LocomotionOutput, &TurnLocomotionOutput>);
    SetRelocationRule<MotionRequest>(world, RelocationRuleKind::Maps,
                                     &RelocateValue<MotionRequest, &TurnMotionRequest>);
    SetRelocationRule<SupportState>(world, RelocationRuleKind::Maps, &RelocateValue<SupportState, &ForgetSupport>);
    SetRelocationRule<Immersion>(world, RelocationRuleKind::Maps, &RelocateValue<Immersion, &ForgetImmersion>);
    SetRelocationRule<TetherSession>(world, RelocationRuleKind::Maps, &RelocateValue<TetherSession, &EndTether>);
    SetRelocationRule<ClingSession>(world, RelocationRuleKind::Maps, &LeaveClingMode);
}
