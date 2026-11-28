#include <movement/MovementRegistration.h>

#include <movement/LocomotionMode.h>
#include <movement/components/HeldMotion.h>
#include <world/relocation/RelocationRules.h>

#include <algorithm>
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

    void ForgetSupport(SupportState& support, const RelocationFrame&) { support = SupportState{}; }
    void ForgetImmersion(Immersion& immersion, const RelocationFrame&) { immersion = Immersion{}; }

    void CarryTetherPoint(TetherSession& tether, const RelocationFrame& frame)
    {
        if (tether.Owner.IsSet() && !tether.Anchor.IsValid())
            tether.Point = frame.MapPoint(tether.Point);
    }

    void CarryOrLeaveCling(void*, World& world, EntityId entity, const RelocationFrame& frame)
    {
        ClingSession* cling = world.TryGet<ClingSession>(entity);
        if (cling == nullptr)
            return;
        if (std::ranges::find(frame.Group, cling->Surface) != frame.Group.end())
        {
            cling->Anchor = frame.MapPoint(cling->Anchor);
            cling->SurfaceNormal = frame.MapVector(cling->SurfaceNormal);
            return;
        }
        if (const LocomotionModeRegistry* modes = std::as_const(world).TryGetResource<LocomotionModeRegistry>())
            (void)RequestLocomotionMode(world, entity, modes->FreeMode(), ModeRequestClass::Forced);
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
    SetRelocationRule<TetherSession>(world, RelocationRuleKind::Maps,
                                     &RelocateValue<TetherSession, &CarryTetherPoint>);
    SetRelocationRule<ClingSession>(world, RelocationRuleKind::Maps, &CarryOrLeaveCling);
}
