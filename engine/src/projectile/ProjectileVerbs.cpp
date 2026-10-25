#include <projectile/ProjectileVerbs.h>

#include <authored/AuthoredValue.h>
#include <controller/Aim.h>
#include <ecs/World.h>
#include <projectile/ProjectileHit.h>
#include <projectile/ProjectileSystem.h>

#include <cmath>
#include <string>
#include <utility>

namespace
{
DataFieldSchema Number(std::string key, std::string label, std::string summary, double fallback, double minimum)
{
    DataFieldSchema field = MakeDataField(DataFieldKind::Float, std::move(key), std::move(label), std::move(summary));
    field.Required = false;
    field.Default = fallback;
    field.Numeric.Minimum = minimum;
    return field;
}

DataFieldSchema Vector(std::string key, std::string label, std::string summary)
{
    DataFieldSchema field = MakeDataField(DataFieldKind::Vector, std::move(key), std::move(label), std::move(summary));
    field.VectorLength = 3;
    field.Required = false;
    return field;
}

Vec3d VectorOf(const AuthoredVectorValue& value)
{
    return Vec3d(static_cast<float>(value.Components[0]), static_cast<float>(value.Components[1]),
                 static_cast<float>(value.Components[2]));
}
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
    launch.Arguments.Children.push_back(MakeDataField(DataFieldKind::String, "projectile", "Projectile",
                                                      "The prefab, as asset://<path>.sscene. Its root carries "
                                                      "Projectile."));
    launch.Arguments.Children.push_back(Vector("origin", "Origin", "Where it starts. The instigator's aim when absent."));
    DataFieldSchema direction = Vector("direction", "Direction", "Which way it goes. The instigator's aim when absent.");
    direction.Editor.Widget = "direction";
    launch.Arguments.Children.push_back(std::move(direction));
    launch.Arguments.Children.push_back(
        Number("spread", "Spread", "Degrees either side of the direction it may scatter by.", 0.0, 0.0));
    launch.Arguments.Children.push_back(
        Number("speed_scale", "Speed scale", "Multiplies the projectile's own speed.", 1.0, 0.0));
    launch.Arguments.Children.push_back(Number(
        "magnitude", "Magnitude", "What the launcher measured -- a charge, a distance -- scaling the projectile's "
                                  "scaled effect. Zero applies none.",
        0.0, 0.0));
    launch.Arguments.Children.push_back(
        Number("splash_scale", "Splash scale", "Multiplies the projectile's splash radius; zero has none.", 1.0, 0.0));
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
    hit.Instigator = "instigator";
    (void)events.Declare(std::move(hit));
}

VerbAdmission InvokeProjectileLaunch(ProjectileSystem& system, const VerbInvocation& invocation)
{
    const AuthoredArguments* arguments = invocation.Arguments;
    EntityId instigator;
    std::string_view path;
    double spread = 0.0;
    double speedScale = 1.0;
    double magnitude = 0.0;
    double splashScale = 1.0;
    if (arguments == nullptr || !arguments->TryGetEntity(0, instigator) || !arguments->TryGetString(1, path)
        || !arguments->TryGetFloat(4, spread) || !arguments->TryGetFloat(5, speedScale)
        || !arguments->TryGetFloat(6, magnitude) || !arguments->TryGetFloat(7, splashScale))
        return VerbAdmission::InvalidArguments;

    ProjectileLaunch launch{ .Prefab = system.PrefabOf(path),
                             .Instigator = instigator,
                             .Tick = invocation.Tick,
                             .SpreadDegrees = static_cast<float>(spread),
                             .SpeedScale = static_cast<float>(speedScale),
                             .Magnitude = static_cast<float>(magnitude),
                             .SplashScale = static_cast<float>(splashScale) };
    if (!launch.Prefab.IsValid())
        return VerbAdmission::UnresolvedReference;

    const World& world = system.GetWorld();
    AuthoredVectorValue origin;
    AuthoredVectorValue direction;
    const bool hasOrigin = arguments->TryGetVector(2, origin) && origin.Length == 3;
    const bool hasDirection = arguments->TryGetVector(3, direction) && direction.Length == 3;
    if ((!hasOrigin || !hasDirection) && world.IsAlive(instigator))
    {
        const Aim aim = AimOf(world, instigator);
        launch.Origin = aim.Origin;
        launch.Direction = aim.Direction;
    }
    if (hasOrigin)
        launch.Origin = VectorOf(origin);
    if (hasDirection)
        launch.Direction = VectorOf(direction);

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
