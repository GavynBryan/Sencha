#include <camera/CameraFovScaleVerbs.h>

#include <camera/CameraEffectArguments.h>
#include <camera/CameraFovScale.h>
#include <ecs/World.h>
#include <time/SimClock.h>

VerbAdmission CameraFovScaleOperations::Pulse(const VerbInvocation& invocation, float scale, float attack, float hold,
                                              float release, std::optional<EntityId> subject)
{
    return Start(invocation, false, scale, CameraEnvelope{ .Attack = attack, .Hold = hold, .Release = release },
                 subject);
}

VerbAdmission CameraFovScaleOperations::Hold(const VerbInvocation& invocation, float scale, float attack, float hold,
                                             float release, std::optional<EntityId> subject)
{
    return Start(invocation, true, scale, CameraEnvelope{ .Attack = attack, .Hold = hold, .Release = release },
                 subject);
}

VerbAdmission CameraFovScaleOperations::EndHold(const VerbInvocation& invocation)
{
    if (CameraFovScaleState* state = Entities->TryGetResource<CameraFovScaleState>())
        state->Release(invocation.Owner, CameraEffectTickOf(invocation, *Clock), Clock->GetFixedDt());
    return VerbAdmission::Accepted;
}

VerbAdmission CameraFovScaleOperations::Start(const VerbInvocation& invocation, bool held, float scale,
                                              const CameraEnvelope& envelope, std::optional<EntityId> subject)
{
    if (!IsValidCameraEnvelope(envelope))
        return VerbAdmission::InvalidArguments;
    CameraFovScaleState* state = Entities->TryGetResource<CameraFovScaleState>();
    if (state == nullptr)
        return VerbAdmission::Unavailable;
    const EntityId viewer = subject.value_or(EntityId{});
    const std::uint64_t tick = CameraEffectTickOf(invocation, *Clock);
    state->Start({ .Timing = held ? CameraEffectTiming::Held(envelope, tick, viewer, invocation.Owner)
                                  : CameraEffectTiming::Once(envelope, tick, viewer),
                   .Scale = scale },
                 { .Tick = tick + 1, .Alpha = 0.0, .TickSeconds = Clock->GetFixedDt() });
    return VerbAdmission::Accepted;
}
