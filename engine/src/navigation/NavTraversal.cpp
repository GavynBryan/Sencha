#include <navigation/NavTraversal.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/MovementIntent.h>
#include <movement/components/CharacterFacts.h>
#include <navigation/NavRouteFollowing.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>

namespace
{
    // Landing counts within this many metres of the exit, or the link's own
    // radius when that is larger: a jump's landing is not a corner.
    constexpr float kLandingRadius = 1.0f;
    // A jump that has not left the ground after this many ticks never will.
    constexpr std::uint32_t kTakeOffTicks = 30;
    constexpr std::uint32_t kAirborne = 1u << 0;

    struct Footing
    {
        Vec3d Position = Vec3d::Zero();
        bool Standing = false;
    };

    Footing FootingOf(const World& world, EntityId agent)
    {
        Footing footing;
        if (const WorldTransform* pose = world.TryGet<WorldTransform>(agent))
            footing.Position = pose->Value.Position;
        if (const SupportState* support = world.TryGet<SupportState>(agent))
            footing.Standing = support->Kind == SupportKind::Stable;
        return footing;
    }

    void SteerToward(MovementIntent& intent, const Vec3d& from, const Vec3d& to)
    {
        Vec3d direction(to.X - from.X, 0.0f, to.Z - from.Z);
        const float length = std::sqrt(direction.X * direction.X + direction.Z * direction.Z);
        intent.WishDir = length > 1e-4f ? direction * (1.0f / length) : Vec3d::Zero();
    }

    bool AtExit(const NavTraversalFrame& frame, const Vec3d& position)
    {
        return NavHasArrived(position, frame.Step.Exit, std::max(frame.Step.EntryRadius, kLandingRadius), frame.Height);
    }
}

bool NavTraversalExecutors::Register(GameplayTagId kind, NavTraversalExecutor executor)
{
    if (!kind.IsValid() || executor.Step == nullptr)
        return false;
    Executors[kind] = executor;
    return true;
}

const NavTraversalExecutor* NavTraversalExecutors::Resolve(const GameplayTagRegistry& tags, GameplayTagId kind) const
{
    for (GameplayTagId current = kind; current.IsValid(); current = tags.GetParent(current))
        if (const auto found = Executors.find(current); found != Executors.end())
            return &found->second;
    return nullptr;
}

bool RegisterNavTraversal(World& world, GameplayTagId kind, NavTraversalExecutor executor)
{
    if (!world.HasResource<NavTraversalExecutors>())
        world.AddResource<NavTraversalExecutors>();
    return world.GetResource<NavTraversalExecutors>().Register(kind, executor);
}

NavTraversalResult NavTraverseByWalking(NavTraversalFrame& frame)
{
    const Footing footing = FootingOf(frame.Entities, frame.Agent);
    frame.Intent.Jump = false;
    if (footing.Standing && AtExit(frame, footing.Position))
    {
        frame.Intent.WishDir = Vec3d::Zero();
        return NavTraversalResult::Succeeded;
    }
    SteerToward(frame.Intent, footing.Position, frame.Step.Exit);
    return NavTraversalResult::Running;
}

NavTraversalResult NavTraverseByJumping(NavTraversalFrame& frame)
{
    const Footing footing = FootingOf(frame.Entities, frame.Agent);
    if (!footing.Standing)
        frame.Memory |= kAirborne;
    SteerToward(frame.Intent, footing.Position, frame.Step.Exit);
    if ((frame.Memory & kAirborne) == 0)
    {
        frame.Intent.Jump = true;
        return frame.Ticks < kTakeOffTicks ? NavTraversalResult::Running : NavTraversalResult::Failed;
    }
    frame.Intent.Jump = false;
    if (!footing.Standing)
        return NavTraversalResult::Running;
    return AtExit(frame, footing.Position) ? NavTraversalResult::Succeeded : NavTraversalResult::Failed;
}
