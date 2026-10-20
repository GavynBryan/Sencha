#pragma once

#include <ecs/ComponentAnnotations.h>
#include <physics/CollisionShape.h>
#include <physics/PhysicsTypes.h>
#include <world/serialization/SceneComponentValidation.h>

// Collision geometry in body space. A sibling RigidBody supplies motion;
// otherwise the body is static. Mesh is a runtime collision-cache handle.
struct SENCHA_COMPONENT("sencha.physics.collider") SENCHA_SCHEMA("collider") SENCHA_SCENE_CHUNK("CLDR") Collider
{
    // A valid Mesh (a cooked shape resolved into the CollisionShapeCache, the
    // static-brush-collision case) takes precedence over the inline primitive
    // Shape used by authored/runtime colliders.
    SENCHA_FIELD("shape")
    CollisionShape Shape{};
    CollisionShapeHandle Mesh{};
    SENCHA_FIELD("is_trigger")
    bool IsTrigger = false;
    SENCHA_FIELD("shape_pose")
    BodyTransform ShapePose{ Vec3d::Zero(), Quatf::Identity() };
};

template <> struct SceneComponentValidation<Collider>
{
    template <typename Archive>
    static bool Validate(const Collider& collider, Archive& archive)
    {
        if constexpr (requires { archive.HasField("mesh"); })
            if (archive.IsText() && archive.HasField("mesh"))
                archive.MarkInvalidField("mesh: authored mesh collider references are not supported");
        if (collider.Mesh.IsValid())
            archive.MarkInvalidField("mesh: runtime collision cache handles cannot be saved as authored colliders");
        if (!IsValidCollisionShape(collider.Shape))
            archive.MarkInvalidField("shape: expected finite positive primitive dimensions");
        if (!IsValidBodyTransform(collider.ShapePose))
            archive.MarkInvalidField("shape_pose: expected finite position and unit rotation");
        return archive.Ok();
    }
};

#if !defined(SENCHA_CODEGEN)
#  include <physics/components/Collider.sencha.h>
#endif
