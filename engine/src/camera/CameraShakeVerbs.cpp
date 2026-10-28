#include <camera/CameraShakeVerbs.h>

#include <assets/data/DataAssetCache.h>
#include <authored/AuthoredValue.h>
#include <camera/CameraEffectArguments.h>
#include <camera/CameraShake.h>
#include <core/assets/AssetRef.h>
#include <ecs/World.h>
#include <time/SimClock.h>

#include <cmath>
#include <string>

namespace
{
namespace Slot
{
constexpr std::size_t Profile = 0;
constexpr std::size_t Scale = 1;
constexpr std::size_t Subject = 2;
constexpr std::size_t Point = 3;
constexpr std::size_t Seed = 4;
} // namespace Slot

VerbDefinition Cosmetic(std::string_view name, std::string displayName, std::string description, VerbKind kind)
{
    VerbDefinition verb = MakeCosmeticVerb(name, std::move(displayName), std::move(description), "Camera", kind);
    DataFieldSchema profile = MakeDataField(DataFieldKind::DataAssetRef, "profile", "Profile", "How the view shakes.");
    profile.Reference.DataSubtype = std::string(kCameraShakeType);
    DataFieldSchema scale = MakeDataField(DataFieldKind::Float, "scale", "Scale", "Multiplies the profile.");
    scale.Required = false;
    scale.Default = 1.0;
    scale.Numeric.Minimum = 0.0;
    DataFieldSchema point = MakeDataField(DataFieldKind::Vector, "point", "Point",
                                          "Where it comes from, for a profile that falls off with distance.");
    point.Required = false;
    DataFieldSchema seed = MakeDataField(DataFieldKind::Int, "seed", "Seed", "Picks the noise.");
    seed.Required = false;
    seed.Default = std::int64_t{ 0 };
    verb.Arguments.Children = { std::move(profile), std::move(scale), CameraEffectSubjectField(), std::move(point),
                                std::move(seed) };
    return verb;
}
} // namespace

void DeclareCameraShakeVerbs(VerbRegistrationScope& scope)
{
    (void)scope.Declare(Cosmetic(kCameraShakeVerb, "Shake camera", "Shakes the view once.", VerbKind::Instant));
    (void)scope.Declare(Cosmetic(kCameraShakeHoldLease, "Hold camera shake",
                                 "Shakes the view while held, then lets it settle.", VerbKind::Lease));
}

VerbAdmission CameraShakeOperations::Shake(const VerbInvocation& invocation)
{
    return Start(invocation, false);
}

VerbAdmission CameraShakeOperations::Hold(const VerbInvocation& invocation)
{
    if (invocation.Step == LeaseStep::Release)
    {
        if (CameraShakeState* state = Entities->TryGetResource<CameraShakeState>())
            state->Release(invocation.Owner, CameraEffectTickOf(invocation, *Clock), Clock->GetFixedDt());
        return VerbAdmission::Accepted;
    }
    return Start(invocation, true);
}

VerbAdmission CameraShakeOperations::Start(const VerbInvocation& invocation, bool held)
{
    const AuthoredArguments* arguments = invocation.Arguments;
    const AssetRef* reference = nullptr;
    double scale = 1.0;
    if (arguments == nullptr || !arguments->TryGetDataAsset(Slot::Profile, reference) || !arguments->TryGetFloat(Slot::Scale, scale)
        || !std::isfinite(scale) || scale < 0.0 || (held && !invocation.Owner.IsSet()))
        return VerbAdmission::InvalidArguments;
    CameraShakeState* state = Entities->TryGetResource<CameraShakeState>();
    if (state == nullptr)
        return VerbAdmission::Unavailable;
    const CameraShakeProfile* profile =
        Profiles->TryGet<CameraShakeProfile>(Profiles->Find(reference->Path), kCameraShakeType);
    if (profile == nullptr)
        return VerbAdmission::Refused;

    EntityId subject;
    (void)arguments->TryGetEntity(Slot::Subject, subject);
    std::int64_t seed = 0;
    (void)arguments->TryGetInt(Slot::Seed, seed);
    const std::uint64_t tick = CameraEffectTickOf(invocation, *Clock);
    CameraShakeInstance shake{ .Timing = held ? CameraEffectTiming::Held(profile->Envelope, tick, subject, invocation.Owner)
                                              : CameraEffectTiming::Once(profile->Envelope, tick, subject),
                               .Profile = *profile,
                               .Scale = static_cast<float>(scale),
                               .Seed = static_cast<std::uint64_t>(seed),
                               .Point = std::nullopt };
    AuthoredVectorValue point;
    if (arguments->TryGetVector(Slot::Point, point) && point.Length == 3)
        shake.Point = Vec3d(static_cast<float>(point.Components[0]), static_cast<float>(point.Components[1]),
                            static_cast<float>(point.Components[2]));
    state->Start(std::move(shake), { .Tick = tick + 1, .Alpha = 0.0, .TickSeconds = Clock->GetFixedDt() });
    return VerbAdmission::Accepted;
}
