#include <projectile/ProjectileVerbs.h>

#include <core/metadata/DataSchemaFields.h>
#include <authored/AuthoredValueTraits.h>
#include <authored/AuthoredValue.h>
#include <controller/Aim.h>
#include <core/assets/AssetRef.h>
#include <ecs/World.h>
#include <projectile/ProjectileHit.h>
#include <projectile/ProjectileSystem.h>

#include <cmath>
#include <string>
#include <utility>

namespace
{
} // namespace

void DeclareProjectileVocabulary(VerbRegistrationScope& verbs, AuthoredEventRegistrationScope& events)
{
    VerbDefinition launch;
    launch.Name = std::string(kProjectileLaunchVerb);
    launch.DisplayName = "Launch projectile";
    launch.Description = "Launches a projectile prefab from the instigator's aim, or from an origin along a "
                         "direction. The authority flies it and decides what it strikes.";
    launch.Category = "Projectiles";
    launch.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "instigator", "Instigator",
                                                      "Who launched it: never struck by it, and credited with "
                                                      "what it strikes."));
    DataFieldSchema projectile = MakeDataField(DataFieldKind::AssetRef, "projectile", "Projectile",
                                               "The prefab: a scene whose root carries Projectile. Content that "
                                               "names it holds it resident.");
    projectile.Reference.AssetTypeFilter = AssetType::Scene;
    launch.Arguments.Children.push_back(std::move(projectile));
    launch.Arguments.Children.push_back(DataField::Vector("origin", "Origin", "Where it starts. The instigator's aim when absent.", 3, false));
    DataFieldSchema direction = DataField::Vector("direction", "Direction", "Which way it goes. The instigator's aim when absent.", 3, false);
    direction.Editor.Widget = "direction";
    launch.Arguments.Children.push_back(std::move(direction));
    launch.Arguments.Children.push_back(
        DataField::OptionalNumber("spread", "Spread", "Degrees either side of the direction it may scatter by.", 0.0, 0.0));
    launch.Arguments.Children.push_back(
        DataField::OptionalNumber("speed_scale", "Speed scale", "Multiplies the projectile's own speed.", 1.0, 0.0));
    launch.Arguments.Children.push_back(DataField::OptionalNumber(
        "magnitude", "Magnitude", "What the launcher measured -- a charge, a distance -- scaling the projectile's "
                                  "scaled effect. Zero applies none.",
        0.0, 0.0));
    launch.Arguments.Children.push_back(
        DataField::OptionalNumber("splash_scale", "Splash scale", "Multiplies the projectile's splash radius; zero has none.", 1.0, 0.0));
    (void)verbs.Declare(std::move(launch));

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

VerbAdmission InvokeProjectileLaunch(ProjectileSystem& system, const VerbInvocation& invocation)
{
    const AuthoredArguments* arguments = invocation.Arguments;
    EntityId instigator;
    const AssetRef* projectile = nullptr;
    double spread = 0.0;
    double speedScale = 1.0;
    double magnitude = 0.0;
    double splashScale = 1.0;
    if (arguments == nullptr || !arguments->TryGetEntity(0, instigator) || !arguments->TryGetAsset(1, projectile)
        || !arguments->TryGetFloat(4, spread) || !arguments->TryGetFloat(5, speedScale)
        || !arguments->TryGetFloat(6, magnitude) || !arguments->TryGetFloat(7, splashScale))
        return VerbAdmission::InvalidArguments;

    ProjectileLaunch launch{ .Prefab = system.PrefabOf(*projectile),
                             .Instigator = instigator,
                             .Tick = invocation.Tick,
                             .SpreadDegrees = static_cast<float>(spread),
                             .SpeedScale = static_cast<float>(speedScale),
                             .Magnitude = static_cast<float>(magnitude),
                             .SplashScale = static_cast<float>(splashScale) };
    if (!launch.Prefab.IsValid())
        return VerbAdmission::UnresolvedReference;

    const World& world = system.GetWorld();
    Vec3d origin;
    Vec3d direction;
    const bool hasOrigin = ReadAuthoredArgument(*arguments, 2, origin);
    const bool hasDirection = ReadAuthoredArgument(*arguments, 3, direction);
    if ((!hasOrigin || !hasDirection) && world.IsAlive(instigator))
    {
        const Aim aim = AimOf(world, instigator);
        launch.Origin = aim.Origin;
        launch.Direction = aim.Direction;
    }
    if (hasOrigin)
        launch.Origin = origin;
    if (hasDirection)
        launch.Direction = direction;

    // The scatter is the launch's own: who, when, and which of that instigator's
    // launches this tick it was.
    launch.Seed = system.NextSeed(instigator, invocation.Tick);

    ProjectileLaunchResult result = ProjectileLaunchResult::Refused;
    (void)system.Launch(launch, &result);
    switch (result)
    {
    case ProjectileLaunchResult::Launched: return VerbAdmission::Accepted;
    case ProjectileLaunchResult::Full: return VerbAdmission::QueueFull;
    case ProjectileLaunchResult::Unresolved: return VerbAdmission::UnresolvedReference;
    case ProjectileLaunchResult::Refused: break;
    }
    return VerbAdmission::Refused;
}
