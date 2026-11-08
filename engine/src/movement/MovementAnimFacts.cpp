#include <movement/MovementAnimFacts.h>

#include <anim/AnimFactProviders.h>
#include <math/Vec.h>
#include <movement/components/CharacterFacts.h>
#include <world/transform/TransformComponents.h>

#include <cmath>

namespace
{
    bool ReadGrounded(const World& world, EntityId entity, const void*, std::uint32_t& out)
    {
        if (!world.IsRegistered<SupportState>())
            return false;
        const SupportState* support = world.TryGet<SupportState>(entity);
        if (support == nullptr)
            return false;
        out = AnimFactFromBool(support->Kind == SupportKind::Stable);
        return true;
    }

    const KinematicState* FindKinematics(const World& world, EntityId entity)
    {
        return world.IsRegistered<KinematicState>() ? world.TryGet<KinematicState>(entity)
                                                    : nullptr;
    }

    bool ReadSpeed(const World& world, EntityId entity, const void*, std::uint32_t& out)
    {
        const KinematicState* kinematics = FindKinematics(world, entity);
        if (kinematics == nullptr)
            return false;
        // Planar: world up is Y, and vertical motion is its own slot.
        const double x = kinematics->Velocity.X;
        const double z = kinematics->Velocity.Z;
        out = AnimFactFromFloat(static_cast<float>(std::sqrt(x * x + z * z)));
        return true;
    }

    // Planar velocity along one of the entity's own axes, by the rotation it is drawn with.
    bool ReadVelocityAlong(const World& world, EntityId entity, const Vec3d& axis, std::uint32_t& out)
    {
        const KinematicState* kinematics = FindKinematics(world, entity);
        const WorldTransform* pose =
            world.IsRegistered<WorldTransform>() ? world.TryGet<WorldTransform>(entity) : nullptr;
        if (kinematics == nullptr || pose == nullptr)
            return false;
        Vec3d along = pose->Value.Rotation.RotateVector(axis);
        along.Y = 0.0f;
        const float length = along.Magnitude();
        out = AnimFactFromFloat(length > 1.0e-4f ? kinematics->Velocity.Dot(along / length) : 0.0f);
        return true;
    }

    bool ReadMoveForward(const World& world, EntityId entity, const void*, std::uint32_t& out)
    {
        return ReadVelocityAlong(world, entity, Vec3d::Forward(), out);
    }

    bool ReadMoveRight(const World& world, EntityId entity, const void*, std::uint32_t& out)
    {
        return ReadVelocityAlong(world, entity, Vec3d::Right(), out);
    }

    bool ReadVerticalSpeed(const World& world, EntityId entity, const void*, std::uint32_t& out)
    {
        const KinematicState* kinematics = FindKinematics(world, entity);
        if (kinematics == nullptr)
            return false;
        out = AnimFactFromFloat(static_cast<float>(kinematics->Velocity.Y));
        return true;
    }
}

bool BindMovementAnimFacts(AnimFactProviders& providers)
{
    bool bound = providers.Bind("Grounded", AnimFactKind::Bool, &ReadGrounded);
    bound = providers.Bind("Speed", AnimFactKind::Float, &ReadSpeed) && bound;
    bound = providers.Bind("VerticalSpeed", AnimFactKind::Float, &ReadVerticalSpeed) && bound;
    bound = providers.Bind("MoveForward", AnimFactKind::Float, &ReadMoveForward) && bound;
    bound = providers.Bind("MoveRight", AnimFactKind::Float, &ReadMoveRight) && bound;
    return bound;
}
