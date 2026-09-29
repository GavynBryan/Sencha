#include <movement/MovementAnimFacts.h>

#include <anim/AnimFactProviders.h>
#include <movement/components/CharacterFacts.h>

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
    return bound;
}
