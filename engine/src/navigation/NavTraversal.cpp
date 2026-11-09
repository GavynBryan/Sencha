#include <navigation/NavTraversal.h>

#include <abilities/AbilityRegistry.h>
#include <abilities/AbilityRequestQueue.h>
#include <abilities/AbilityActivations.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/MovementIntent.h>
#include <movement/components/CharacterFacts.h>
#include <navigation/NavRouteFollowing.h>
#include <navigation/ZoneNavigation.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>

namespace
{
    // Ticks a requested ability may take to appear before its absence means it
    // never started.
    constexpr std::uint32_t kAbilityStartTicks = 2;
    // How far above standing height over the exit a rope aims to let go, and how
    // far to the side of that it may and still drop within the landing radius.
    constexpr float kReleaseHeight = 0.5f;
    constexpr float kReleaseReach = 0.9f;
    // A rope lets go at least this far short of its anchor.
    constexpr float kReleaseFromAnchor = 1.0f;

    void ForgetCrossing(World& world, EntityId agent)
    {
        if (NavAbilityCrossings* crossings = world.TryGetResource<NavAbilityCrossings>())
            std::erase_if(crossings->Active, [&](const NavAbilityCrossing& entry) { return entry.Agent == agent; });
    }

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

    bool IsAtExit(const NavTraversalFrame& frame, const Vec3d& position)
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
    if (footing.Standing && IsAtExit(frame, footing.Position))
    {
        frame.Intent.WishDir = Vec3d::Zero();
        return NavTraversalResult::Succeeded;
    }
    frame.Intent.WishDir = NavPlanarDirection(footing.Position, frame.Step.Exit);
    return NavTraversalResult::Running;
}

NavTraversalResult NavTraverseByJumping(NavTraversalFrame& frame)
{
    const Footing footing = FootingOf(frame.Entities, frame.Agent);
    if (!footing.Standing)
        frame.Memory |= kAirborne;
    frame.Intent.WishDir = NavPlanarDirection(footing.Position, frame.Step.Exit);
    if ((frame.Memory & kAirborne) == 0)
    {
        frame.Intent.Jump = true;
        return frame.Ticks < kTakeOffTicks ? NavTraversalResult::Running : NavTraversalResult::Failed;
    }
    frame.Intent.Jump = false;
    if (!footing.Standing)
        return NavTraversalResult::Running;
    return IsAtExit(frame, footing.Position) ? NavTraversalResult::Succeeded : NavTraversalResult::Failed;
}

NavTraversalResult NavTraverseByAbility(NavTraversalFrame& frame)
{
    World& world = frame.Entities;
    frame.Intent.WishDir = Vec3d::Zero();
    frame.Intent.Jump = false;
    if (frame.Memory == 0)
    {
        const AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
        AbilityRequestQueue* queue = world.TryGetResource<AbilityRequestQueue>();
        const AbilityId ability = abilities != nullptr && !frame.Ability.empty() ? abilities->Find(frame.Ability) : AbilityId{};
        if (queue == nullptr || !ability.IsValid())
            return NavTraversalResult::Failed;
        const NavRouteStep& step = frame.Step;
        const Vec3d centre = Vec3d::Up() * (frame.Height * 0.5f);
        NavAbilityCrossing crossing{ .Agent = frame.Agent, .Start = step.Entry + centre, .Exit = step.Exit + centre,
                                     .Anchor = step.Anchor, .Release = step.Exit + centre, .Anchored = step.Anchored };
        if (step.Anchored)
        {
            const std::optional<Vec3d> release = NavAnchoredRelease(step.Entry, step.Exit, step.Anchor, frame.Height);
            if (!release)
                return NavTraversalResult::Failed;
            crossing.Release = *release;
        }
        queue->Push({ .Kind = AbilityRequestKind::Activate, .Actor = frame.Agent, .Ability = ability,
                      .Instigator = frame.Agent });
        if (!world.HasResource<NavAbilityCrossings>())
            world.AddResource<NavAbilityCrossings>();
        ForgetCrossing(world, frame.Agent);
        world.GetResource<NavAbilityCrossings>().Active.push_back(crossing);
        frame.Memory = ability.Value + 1;
        return NavTraversalResult::Running;
    }
    if (IsAbilityLive(world, frame.Agent, AbilityId{ frame.Memory - 1 }) || frame.Ticks < kAbilityStartTicks)
        return NavTraversalResult::Running;
    // An ability may end in the air, as a rope let go does; the crossing lasts until it lands.
    const Footing footing = FootingOf(world, frame.Agent);
    if (!footing.Standing)
        return NavTraversalResult::Running;
    ForgetCrossing(world, frame.Agent);
    return IsAtExit(frame, footing.Position) ? NavTraversalResult::Succeeded : NavTraversalResult::Failed;
}

void NavAbortAbilityTraversal(World& world, EntityId agent, const NavRouteStep&, std::uint32_t memory)
{
    ForgetCrossing(world, agent);
    if (memory == 0)
        return;
    if (AbilityRequestQueue* queue = world.TryGetResource<AbilityRequestQueue>())
        queue->Push({ .Kind = AbilityRequestKind::Cancel, .Actor = agent, .Ability = AbilityId{ memory - 1 },
                      .Instigator = agent });
}

std::size_t CollectNavCapabilities(const World& world, EntityId agent, const ZoneNavigation& navigation,
                                   std::span<GameplayTagId> out)
{
    const NavTraversalExecutors* table = world.TryGetResource<NavTraversalExecutors>();
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    const GameplayTagContainer* held =
        world.IsRegistered<GameplayTagContainer>() ? world.TryGet<GameplayTagContainer>(agent) : nullptr;
    if (table == nullptr || tags == nullptr || held == nullptr)
        return 0;
    const auto granted = [&](GameplayTagId kind) {
        for (std::uint8_t i = 0; i < held->Count; ++i)
            if (tags->IsDescendantOf(kind, held->Tags[i]))
                return true;
        return false;
    };
    std::size_t count = 0;
    for (std::uint32_t i = 0; i < navigation.LinkCount(); ++i)
    {
        const GameplayTagId kind = navigation.Link(i).Traversal;
        const auto chosen = out.first(count);
        // The zone binds at most kNavMaxTraversalKinds kinds; past that a kind is unusable anyway.
        if (!kind.IsValid() || count == out.size() || std::ranges::find(chosen, kind) != chosen.end())
            continue;
        if (granted(kind) && table->Resolve(*tags, kind) != nullptr)
            out[count++] = kind;
    }
    return count;
}

std::optional<Vec3d> NavAnchoredRelease(const Vec3d& entry, const Vec3d& exit, const Vec3d& anchor, float height)
{
    const Vec3d up = Vec3d::Up();
    const Vec3d centre = up * (height * 0.5f);
    const Vec3d start = entry + centre;
    const Vec3d above = exit + centre + up * kReleaseHeight;
    const Vec3d rope = anchor - start;
    const float length = rope.Magnitude();
    if (length <= kReleaseFromAnchor)
        return std::nullopt;
    const Vec3d direction = rope / length;
    const float along = std::clamp((above - start).Dot(direction), 0.0f, length - kReleaseFromAnchor);
    const Vec3d release = start + direction * along;
    const Vec3d off = release - above;
    if (std::sqrt(off.X * off.X + off.Z * off.Z) > kReleaseReach || release.Y < exit.Y + centre.Y)
        return std::nullopt;
    return release;
}
