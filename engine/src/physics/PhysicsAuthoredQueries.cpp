#include <physics/PhysicsAuthoredQueries.h>

#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredSchema.h>
#include <authored/AuthoredValueTraits.h>
#include <physics/PhysicsQueries.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    template<typename T>
    DataFieldSchema QueryField(std::string key, std::string label, std::string summary = {})
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::String, std::move(key), std::move(label),
                                              std::move(summary));
        AuthoredValueTraits<T>::Describe(field);
        return field;
    }

    DataFieldSchema IgnoreField()
    {
        return QueryField<std::optional<EntityId>>("ignore", "Ignore", "An entity whose bodies the query passes through.");
    }

    // Triggers count: an authored trace or overlap reports what is there. Views
    // `ignore`, which must outlive the query.
    PhysicsQueryFilter Filter(const std::optional<EntityId>& ignore)
    {
        return PhysicsQueryFilter{ .IncludeTriggers = true,
                                   .IgnoreEntities = ignore ? std::span(&*ignore, 1) : std::span<const EntityId>{} };
    }

    template<typename T>
    bool Read(std::span<const AuthoredValue> arguments, std::size_t slot, T& out)
    {
        return AuthoredValueTraits<T>::Decode(AuthoredArgumentAt(arguments, slot), out);
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
        QueryField<Vec3d>("origin", "Origin"),
        QueryField<Vec3d>("direction", "Direction"),
        QueryField<float>("range", "Range", "How far the ray reaches, in meters."),
        IgnoreField(),
    };
    trace.Result = AuthoredRecordRoot();
    trace.Result.Children = {
        QueryField<bool>("hit", "Hit", "Whether the ray struck anything."),
        QueryField<EntityId>("entity", "Entity", "What it struck, when an entity owns the body."),
        QueryField<Vec3d>("point", "Point"),
        QueryField<Vec3d>("normal", "Normal"),
    };
    (void)scope.Declare(std::move(trace));

    AuthoredQueryDefinition overlap;
    overlap.Name = std::string(kPhysicsOverlapQuery);
    overlap.DisplayName = "Overlap";
    overlap.Description = "Every entity whose body overlaps a sphere.";
    overlap.Category = "Physics";
    overlap.Arguments.Children = {
        QueryField<Vec3d>("position", "Position"),
        QueryField<float>("radius", "Radius", "In meters."),
        IgnoreField(),
    };
    overlap.Result = MakeDataField(DataFieldKind::Array, "entities", "Entities");
    overlap.Result.Children = { QueryField<EntityId>("entity", "Entity") };
    (void)scope.Declare(std::move(overlap));
}

AuthoredQueryStatus EvaluatePhysicsTrace(const PhysicsQueries& physics,
                                         std::span<const AuthoredValue> arguments,
                                         AuthoredValue& result)
{
    Vec3d origin;
    Vec3d direction;
    float range = 0.0f;
    std::optional<EntityId> ignore;
    if (!Read(arguments, 0, origin) || !Read(arguments, 1, direction) || !Read(arguments, 2, range)
        || !Read(arguments, 3, ignore) || !(range >= 0.0f) || direction.SqrMagnitude() == 0.0)
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
    if (!Read(arguments, 0, position) || !Read(arguments, 1, radius) || !Read(arguments, 2, ignore)
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
