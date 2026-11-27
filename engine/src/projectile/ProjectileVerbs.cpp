#include <projectile/ProjectileVerbs.h>

#include <authored/AuthoredApi.h>
#include <controller/Aim.h>
#include <core/assets/AssetRef.h>
#include <ecs/World.h>
#include <projectile/ProjectileHit.h>
#include <projectile/ProjectileSystem.h>

#include <string>
#include <utility>

void DeclareProjectileVocabulary(VerbRegistrationScope& verbs, AuthoredEventRegistrationScope& events)
{
    DeclareAuthoredApi<ProjectileOperations>(&verbs, nullptr);

    AuthoredEventDefinition hit;
    hit.Name = std::string(kProjectileHitEvent);
    hit.DisplayName = "Struck by projectile";
    hit.Description = "A projectile struck this entity directly. Splash is not a strike.";
    hit.Category = "Projectiles";
    hit.Payload.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "instigator", "Instigator", "Who launched it."));
    hit.Payload.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "projectile", "Projectile", "What struck, while it lasts."));
    hit.Payload.Children.push_back(
        MakeDataField(DataFieldKind::GameplayTag, "kind", "Kind", "The projectile's hit kind."));
    DataFieldSchema point = MakeDataField(DataFieldKind::Vector, "point", "Point", "Where it struck.");
    point.VectorLength = 3;
    hit.Payload.Children.push_back(std::move(point));
    DataFieldSchema normal = MakeDataField(DataFieldKind::Vector, "normal", "Normal", "The struck surface's normal.");
    normal.VectorLength = 3;
    hit.Payload.Children.push_back(std::move(normal));
    hit.Payload.Children.push_back(MakeDataField(DataFieldKind::GameplayTag, "region", "Region",
                                                 "The struck entity's hit region; none when it has none there."));
    hit.Instigator = "instigator";
    (void)events.Declare(std::move(hit));
}

VerbAdmission ProjectileOperations::Launch(const VerbInvocation& invocation, EntityId instigator,
                                          const AssetRef& projectile, std::optional<Vec3d> origin,
                                          std::optional<Vec3d> direction, float spread, float speedScale,
                                          float magnitude, float splashScale)
{
    ProjectileLaunch launch{ .Prefab = System->PrefabOf(projectile),
                             .Instigator = instigator,
                             .Tick = invocation.Tick,
                             .SpreadDegrees = spread,
                             .SpeedScale = speedScale,
                             .Magnitude = magnitude,
                             .SplashScale = splashScale };
    if (!launch.Prefab.IsValid())
        return VerbAdmission::UnresolvedReference;

    const World& world = System->GetWorld();
    if ((!origin.has_value() || !direction.has_value()) && world.IsAlive(instigator))
    {
        const Aim aim = AimOf(world, instigator);
        launch.Origin = aim.Origin;
        launch.Direction = aim.Direction;
    }
    if (origin.has_value())
        launch.Origin = *origin;
    if (direction.has_value())
        launch.Direction = *direction;
    launch.Seed = System->NextSeed(instigator, invocation.Tick);

    ProjectileLaunchResult result = ProjectileLaunchResult::Refused;
    (void)System->Launch(launch, &result);
    return ProjectileLaunchAdmission(result);
}

VerbAdmission ProjectileLaunchAdmission(ProjectileLaunchResult result)
{
    switch (result)
    {
    case ProjectileLaunchResult::Launched: return VerbAdmission::Accepted;
    case ProjectileLaunchResult::Full: return VerbAdmission::QueueFull;
    case ProjectileLaunchResult::Unresolved: return VerbAdmission::UnresolvedReference;
    case ProjectileLaunchResult::Refused: break;
    }
    return VerbAdmission::Refused;
}
