#include <camera/CameraShakeVerbs.h>

#include <assets/data/DataAssetCache.h>
#include <authored/VerbDispatcher.h>
#include <camera/CameraEffectArguments.h>
#include <camera/CameraShake.h>
#include <core/assets/AssetRef.h>
#include <ecs/World.h>
#include <time/SimClock.h>

#include <utility>

VerbAdmission CameraShakeOperations::Shake(const VerbInvocation& invocation, AuthoredDataRef profile, float scale,
                                           std::optional<EntityId> subject, std::optional<Vec3d> point,
                                           std::int64_t seed)
{
    return Start(invocation, false, profile, scale, subject, point, seed);
}

VerbAdmission CameraShakeOperations::Hold(const VerbInvocation& invocation, AuthoredDataRef profile, float scale,
                                          std::optional<EntityId> subject, std::optional<Vec3d> point,
                                          std::int64_t seed)
{
    return Start(invocation, true, profile, scale, subject, point, seed);
}

VerbAdmission CameraShakeOperations::EndHold(const VerbInvocation& invocation)
{
    if (CameraShakeState* state = Entities->TryGetResource<CameraShakeState>())
        state->Release(invocation.Owner, CameraEffectTickOf(invocation, *Clock), Clock->GetFixedDt());
    return VerbAdmission::Accepted;
}

VerbAdmission CameraShakeOperations::Start(const VerbInvocation& invocation, bool held, AuthoredDataRef profile,
                                           float scale, std::optional<EntityId> subject, std::optional<Vec3d> point,
                                           std::int64_t seed)
{
    CameraShakeState* state = Entities->TryGetResource<CameraShakeState>();
    const DataAssetCache* profiles = Verbs->GetDataAssets();
    if (state == nullptr || profiles == nullptr)
        return VerbAdmission::Unavailable;
    const CameraShakeProfile* shape =
        profiles->TryGet<CameraShakeProfile>(profiles->Find(profile.Reference->Path), kCameraShakeType);
    if (shape == nullptr)
        return VerbAdmission::Refused;

    const EntityId viewer = subject.value_or(EntityId{});
    const std::uint64_t tick = CameraEffectTickOf(invocation, *Clock);
    CameraShakeInstance shake{ .Timing = held ? CameraEffectTiming::Held(shape->Envelope, tick, viewer, invocation.Owner)
                                              : CameraEffectTiming::Once(shape->Envelope, tick, viewer),
                               .Profile = *shape,
                               .Scale = scale,
                               .Seed = static_cast<std::uint64_t>(seed),
                               .Point = point };
    state->Start(std::move(shake), { .Tick = tick + 1, .Alpha = 0.0, .TickSeconds = Clock->GetFixedDt() });
    return VerbAdmission::Accepted;
}
