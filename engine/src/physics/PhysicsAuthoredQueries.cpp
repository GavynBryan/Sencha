#include <physics/PhysicsAuthoredQueries.h>

#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredSchema.h>
#include <authored/AuthoredValueTraits.h>
#include <physics/PhysicsQueries.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    DataFieldSchema IgnoreField()
    {
        return AuthoredField<std::optional<EntityId>>("ignore", "Ignore", "An entity whose bodies the query passes through.");
    }

    // Triggers count: an authored trace or overlap reports what is there. Views
    // `ignore`, which must outlive the query.
    PhysicsQueryFilter Filter(const std::optional<EntityId>& ignore)
    {
        return PhysicsQueryFilter{ .IncludeTriggers = true,
                                   .IgnoreEntities = ignore ? std::span(&*ignore, 1) : std::span<const EntityId>{} };
    }

}

void DeclarePhysicsQueries(AuthoredQueryRegistrationScope& scope)
{
    AuthoredQueryDefinition trace;
    trace.Name = std::string(kPhysicsTraceQuery);
    trace.DisplayName = "Trace";
    trace.Description = "The first body a ray strikes.";
    trace.Category = "Physics";
    trace.Arguments.Children = {
        AuthoredField<Vec3d>("origin", "Origin"),
        AuthoredField<Vec3d>("direction", "Direction"),
        AuthoredField<float>("range", "Range", "How far the ray reaches, in meters."),
        IgnoreField(),
    };
    trace.Result = AuthoredRecordRoot();
    trace.Result.Children = {
        AuthoredField<bool>("hit", "Hit", "Whether the ray struck anything."),
        AuthoredField<EntityId>("entity", "Entity", "What it struck, when an entity owns the body."),
        AuthoredField<Vec3d>("point", "Point"),
        AuthoredField<Vec3d>("normal", "Normal"),
    };
    (void)scope.Declare(std::move(trace));

    AuthoredQueryDefinition overlap;
    overlap.Name = std::string(kPhysicsOverlapQuery);
    overlap.DisplayName = "Overlap";
    overlap.Description = "Every entity whose body overlaps a sphere.";
    overlap.Category = "Physics";
    overlap.Arguments.Children = {
        AuthoredField<Vec3d>("position", "Position"),
        AuthoredField<float>("radius", "Radius", "In meters."),
        IgnoreField(),
    };
    overlap.Result = MakeDataField(DataFieldKind::Array, "entities", "Entities");
    overlap.Result.Children = { AuthoredField<EntityId>("entity", "Entity") };
    (void)scope.Declare(std::move(overlap));

    AuthoredQueryDefinition clear;
    clear.Name = std::string(kPhysicsClearPathQuery);
    clear.DisplayName = "Clear path";
    clear.Description = "Whether a line, or an upright capsule centred on it, reaches the end without meeting "
                        "anything solid. Characters and triggers do not count.";
    clear.Category = "Physics";
    clear.Arguments.Children = {
        AuthoredField<Vec3d>("from", "From"),
        AuthoredField<Vec3d>("to", "To"),
        AuthoredField<float>("radius", "Radius", "The capsule's, in meters; zero tests a line."),
        AuthoredField<float>("height", "Height", "The capsule's whole height, in meters."),
        IgnoreField(),
    };
    clear.Result = AuthoredField<bool>("clear", "Clear");
    (void)scope.Declare(std::move(clear));
}

AuthoredQueryStatus EvaluatePhysicsClearPath(const PhysicsQueries& physics,
                                             std::span<const AuthoredValue> arguments,
                                             AuthoredValue& result)
{
    Vec3d from;
    Vec3d to;
    float radius = 0.0f;
    float height = 0.0f;
    std::optional<EntityId> ignore;
    if (!ReadAuthoredArgument(arguments, 0, from) || !ReadAuthoredArgument(arguments, 1, to) || !ReadAuthoredArgument(arguments, 2, radius)
        || !ReadAuthoredArgument(arguments, 3, height) || !ReadAuthoredArgument(arguments, 4, ignore) || !(radius >= 0.0f) || !(height >= 0.0f))
        return AuthoredQueryStatus::InvalidArguments;

    const Vec3d delta = to - from;
    const float length = delta.Magnitude();
    if (length < 1.0e-4f)
    {
        result = AuthoredValue::Bool(true);
        return AuthoredQueryStatus::Value;
    }
    const PhysicsQueryFilter filter{ .IncludeTriggers = false,
                                     .IgnoreEntities = ignore ? std::span(&*ignore, 1) : std::span<const EntityId>{},
                                     .IncludeCharacters = false };
    const Vec3d direction = delta / length;
    // A line's end is usually on the surface it is aimed at, which is not in the way.
    constexpr float kLineEndInset = 0.25f;
    bool blocked = false;
    if (radius > 0.0f)
        blocked = physics.SweepShape(CollisionShape::MakeCapsule(radius, std::max(height * 0.5f - radius, 0.01f)), from,
                                     Quatf::Identity(), direction, length, filter).Hit;
    else if (length > kLineEndInset)
        blocked = physics.Raycast(from, direction, length - kLineEndInset, filter).Hit;
    result = AuthoredValue::Bool(!blocked);
    return AuthoredQueryStatus::Value;
}

AuthoredQueryStatus EvaluatePhysicsTrace(const PhysicsQueries& physics,
                                         std::span<const AuthoredValue> arguments,
                                         AuthoredValue& result)
{
    Vec3d origin;
    Vec3d direction;
    float range = 0.0f;
    std::optional<EntityId> ignore;
    if (!ReadAuthoredArgument(arguments, 0, origin) || !ReadAuthoredArgument(arguments, 1, direction) || !ReadAuthoredArgument(arguments, 2, range)
        || !ReadAuthoredArgument(arguments, 3, ignore) || !(range >= 0.0f) || direction.SqrMagnitude() == 0.0)
        return AuthoredQueryStatus::InvalidArguments;

    const RaycastHit hit = physics.Raycast(origin, direction.Normalized(), range, Filter(ignore));
    result = AuthoredValue::Record({
        AuthoredValue::Bool(hit.Hit),
        AuthoredValue::Entity(hit.Entity),
        AuthoredValueTraits<Vec3d>::Encode(hit.Point),
        AuthoredValueTraits<Vec3d>::Encode(hit.Normal),
    });
    return AuthoredQueryStatus::Value;
}

AuthoredQueryStatus EvaluatePhysicsOverlap(const PhysicsQueries& physics,
                                           std::span<const AuthoredValue> arguments,
                                           AuthoredValue& result)
{
    Vec3d position;
    float radius = 0.0f;
    std::optional<EntityId> ignore;
    if (!ReadAuthoredArgument(arguments, 0, position) || !ReadAuthoredArgument(arguments, 1, radius) || !ReadAuthoredArgument(arguments, 2, ignore)
        || !(radius > 0.0f))
        return AuthoredQueryStatus::InvalidArguments;

    std::vector<EntityId> entities;
    physics.OverlapShape(CollisionShape::MakeSphere(radius), position, Quatf::Identity(), entities, Filter(ignore));
    std::vector<AuthoredValue> elements;
    elements.reserve(entities.size());
    for (EntityId entity : entities)
        elements.push_back(AuthoredValue::Entity(entity));
    result = AuthoredValue::Array(std::move(elements));
    return AuthoredQueryStatus::Value;
}
