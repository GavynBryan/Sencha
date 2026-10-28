#include <camera/CameraFovScaleVerbs.h>

#include <authored/AuthoredValue.h>
#include <camera/CameraEffectArguments.h>
#include <camera/CameraFovScale.h>
#include <ecs/World.h>
#include <time/SimClock.h>

#include <cmath>
#include <string>

namespace
{
namespace Slot
{
constexpr std::size_t Scale = 0;
constexpr std::size_t Attack = 1;
constexpr std::size_t Hold = 2;
constexpr std::size_t Release = 3;
constexpr std::size_t Subject = 4;
} // namespace Slot

DataFieldSchema Seconds(std::string key, std::string label, std::string summary)
{
    DataFieldSchema field = MakeDataField(DataFieldKind::Float, std::move(key), std::move(label), std::move(summary));
    field.Units = "s";
    field.Required = false;
    field.Default = 0.0;
    field.Numeric.Minimum = 0.0;
    return field;
}

VerbDefinition Cosmetic(std::string_view name, std::string displayName, std::string description, VerbKind kind)
{
    VerbDefinition verb = MakeCosmeticVerb(name, std::move(displayName), std::move(description), "Camera", kind);
    DataFieldSchema scale = MakeDataField(DataFieldKind::Float, "scale", "Scale",
                                          "The multiple of the camera's field of view at full strength: "
                                          "below one narrows, above one widens.");
    scale.Numeric.Minimum = kCameraFovScaleMin;
    scale.Numeric.Maximum = kCameraFovScaleMax;
    verb.Arguments.Children = { std::move(scale), Seconds("attack", "Attack", "Easing to the scale."),
                                Seconds("hold", "Hold", "At the scale, when not held by a lease."),
                                Seconds("release", "Release", "Easing back to the camera's own."),
                                CameraEffectSubjectField() };
    return verb;
}

bool ReadSeconds(const AuthoredArguments& arguments, std::size_t slot, float& out)
{
    double seconds = 0.0;
    if (!arguments.TryGetFloat(slot, seconds))
        return false;
    out = static_cast<float>(seconds);
    return true;
}
} // namespace

void DeclareCameraFovScaleVerbs(VerbRegistrationScope& scope)
{
    (void)scope.Declare(Cosmetic(kCameraFovScaleVerb, "Scale field of view",
                                 "Narrows or widens the view for a moment.", VerbKind::Instant));
    (void)scope.Declare(Cosmetic(kCameraFovScaleHoldLease, "Hold field of view scale",
                                 "Narrows or widens the view while held, then eases back.", VerbKind::Lease));
}

VerbAdmission CameraFovScaleOperations::Pulse(const VerbInvocation& invocation)
{
    return Start(invocation, false);
}

VerbAdmission CameraFovScaleOperations::Hold(const VerbInvocation& invocation)
{
    if (invocation.Step == LeaseStep::Release)
    {
        if (CameraFovScaleState* state = Entities->TryGetResource<CameraFovScaleState>())
            state->Release(invocation.Owner, CameraEffectTickOf(invocation, *Clock), Clock->GetFixedDt());
        return VerbAdmission::Accepted;
    }
    return Start(invocation, true);
}

VerbAdmission CameraFovScaleOperations::Start(const VerbInvocation& invocation, bool held)
{
    const AuthoredArguments* arguments = invocation.Arguments;
    double scale = 1.0;
    CameraEnvelope envelope;
    if (arguments == nullptr || !arguments->TryGetFloat(Slot::Scale, scale) || !ReadSeconds(*arguments, Slot::Attack, envelope.Attack)
        || !ReadSeconds(*arguments, Slot::Hold, envelope.Hold) || !ReadSeconds(*arguments, Slot::Release, envelope.Release)
        || !std::isfinite(scale) || scale < kCameraFovScaleMin || scale > kCameraFovScaleMax
        || !IsValidCameraEnvelope(envelope) || (held && !invocation.Owner.IsSet()))
        return VerbAdmission::InvalidArguments;
    CameraFovScaleState* state = Entities->TryGetResource<CameraFovScaleState>();
    if (state == nullptr)
        return VerbAdmission::Unavailable;

    EntityId subject;
    (void)arguments->TryGetEntity(Slot::Subject, subject);
    const std::uint64_t tick = CameraEffectTickOf(invocation, *Clock);
    state->Start({ .Timing = held ? CameraEffectTiming::Held(envelope, tick, subject, invocation.Owner)
                                  : CameraEffectTiming::Once(envelope, tick, subject),
                   .Scale = static_cast<float>(scale) },
                 { .Tick = tick + 1, .Alpha = 0.0, .TickSeconds = Clock->GetFixedDt() });
    return VerbAdmission::Accepted;
}
