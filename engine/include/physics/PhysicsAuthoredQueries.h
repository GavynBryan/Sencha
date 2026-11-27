#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <physics/PhysicsQueries.h>

#include <optional>
#include <vector>

class PhysicsWorld;

// What physics.trace answers. `Entity` may be invalid on a hit, for a body no entity owns.
struct SENCHA_RECORD PhysicsTraceAnswer
{
    SENCHA_FIELD("hit")
    SENCHA_LABEL("Hit")
    bool Hit = false;

    SENCHA_FIELD("entity")
    SENCHA_LABEL("Entity")
    EntityId Entity{};

    SENCHA_FIELD("point")
    SENCHA_LABEL("Point")
    Vec3d Point = Vec3d::Zero();

    SENCHA_FIELD("normal")
    SENCHA_LABEL("Normal")
    Vec3d Normal = Vec3d::Zero();
};

// The physics queries, answered from a physics world. Triggers count for a trace and an
// overlap, which report what is there; characters and triggers do not count for a clear path.
class PhysicsQueryOperations
{
public:
    explicit PhysicsQueryOperations(const PhysicsWorld& physics)
        : Physics(physics)
    {
    }

    SENCHA_QUERY("physics.trace")
    SENCHA_LABEL("Trace")
    SENCHA_CATEGORY("Physics")
    SENCHA_DESCRIPTION("The first body a ray strikes.")
    std::optional<PhysicsTraceAnswer> Trace(
        SENCHA_ARG("origin") SENCHA_LABEL("Origin") Vec3d origin,
        SENCHA_ARG("direction") SENCHA_LABEL("Direction") Vec3d direction,
        SENCHA_ARG("range") SENCHA_LABEL("Range") SENCHA_DESCRIPTION("How far the ray reaches, in meters.") SENCHA_MIN(0)
            float range,
        SENCHA_ARG("ignore") SENCHA_LABEL("Ignore") SENCHA_DESCRIPTION("An entity whose bodies the query passes through.")
            std::optional<EntityId> ignore = std::nullopt) const;

    SENCHA_QUERY("physics.overlap")
    SENCHA_LABEL("Overlap")
    SENCHA_CATEGORY("Physics")
    SENCHA_DESCRIPTION("Every entity whose body overlaps a sphere.")
    std::vector<EntityId> Overlap(
        SENCHA_ARG("position") SENCHA_LABEL("Position") Vec3d position,
        SENCHA_ARG("radius") SENCHA_LABEL("Radius") SENCHA_DESCRIPTION("In meters.") SENCHA_MIN(0.001) float radius,
        SENCHA_ARG("ignore") SENCHA_LABEL("Ignore") SENCHA_DESCRIPTION("An entity whose bodies the query passes through.")
            std::optional<EntityId> ignore = std::nullopt) const;

    SENCHA_QUERY("physics.clear_path")
    SENCHA_LABEL("Clear path")
    SENCHA_CATEGORY("Physics")
    SENCHA_DESCRIPTION("Whether a line, or an upright capsule centred on it, reaches the end without meeting anything "
                       "solid. Characters and triggers do not count.")
    bool ClearPath(
        SENCHA_ARG("from") SENCHA_LABEL("From") Vec3d from,
        SENCHA_ARG("to") SENCHA_LABEL("To") Vec3d to,
        SENCHA_ARG("radius") SENCHA_LABEL("Radius") SENCHA_DESCRIPTION("The capsule's, in meters; zero tests a line.")
            SENCHA_MIN(0) float radius,
        SENCHA_ARG("height") SENCHA_LABEL("Height") SENCHA_DESCRIPTION("The capsule's whole height, in meters.")
            SENCHA_MIN(0) float height,
        SENCHA_ARG("ignore") SENCHA_LABEL("Ignore") SENCHA_DESCRIPTION("An entity whose bodies the query passes through.")
            std::optional<EntityId> ignore = std::nullopt) const;

private:
    PhysicsQueries Physics;
};

#if !defined(SENCHA_CODEGEN)
#  include <physics/PhysicsAuthoredQueries.sencha.h>
#endif
