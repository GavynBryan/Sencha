#include <world/transform/TransformHistory.h>

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <utility>

void CaptureWorldTransformHistory(World& world,
                                  const StoragePartitionSet& partitions,
                                  bool snapAll)
{
    if (!world.IsRegistered<WorldTransformHistory>() || !world.IsRegistered<WorldTransform>())
        return;

    Query<Write<WorldTransformHistory>, Read<WorldTransform>> query(world);
    query.ForEachChunkIn(partitions, [snapAll](auto& view)
    {
        auto histories = view.template Write<WorldTransformHistory>();
        const auto worlds = view.template Read<WorldTransform>();

        for (uint32_t row = 0; row < view.Count(); ++row)
        {
            WorldTransformHistory& history = histories[row];
            const Transform3f& pose = worlds[row].Value;

            // A snapped entity has no earlier pose worth blending from, so both
            // ends of the blend are this tick and rendering shows it exactly
            // where the simulation put it.
            if (snapAll || history.Snap)
            {
                history.Previous = pose;
                history.Current = pose;
                history.Snap = false;
                continue;
            }

            history.Previous = history.Current;
            history.Current = pose;
        }
    });
}

void RequestTransformHistorySnap(World& world, EntityId entity)
{
    if (!world.IsRegistered<WorldTransformHistory>())
        return;
    if (WorldTransformHistory* history = world.TryGet<WorldTransformHistory>(entity))
        history->Snap = true;
}

namespace
{
// Deeper than any authored rig. A chain that reaches it is a cycle, which
// propagation refuses to order, so the walk stops rather than recursing on.
constexpr int kMaxPresentationDepth = 64;

Transform3f PresentationPoseAt(const World& world, EntityId entity, double alpha, int depth)
{
    if (!entity.IsValid() || !world.IsAlive(entity))
        return Transform3f::Identity();
    if (world.IsRegistered<WorldTransformHistory>())
    {
        if (const WorldTransformHistory* history = world.TryGet<WorldTransformHistory>(entity))
            return ResolvePresentationPose(*history, alpha);
    }
    // What presentation propagation composes: the parent as it is drawn, then
    // this entity's own local transform. Read from the inputs rather than from
    // WorldTransform, which holds whichever domain swept it last -- the tick's
    // pose until this frame's presentation sweep has run.
    const LocalTransform* local =
        world.IsRegistered<LocalTransform>() ? world.TryGet<LocalTransform>(entity) : nullptr;
    if (local != nullptr)
    {
        const Parent* parent = world.IsRegistered<Parent>() ? world.TryGet<Parent>(entity) : nullptr;
        if (parent == nullptr || !parent->Entity.IsValid() || !world.IsAlive(parent->Entity)
            || depth >= kMaxPresentationDepth)
        {
            return local->Value;
        }
        return PresentationPoseAt(world, parent->Entity, alpha, depth + 1) * local->Value;
    }
    if (world.IsRegistered<WorldTransform>())
    {
        if (const WorldTransform* transform = world.TryGet<WorldTransform>(entity))
            return transform->Value;
    }
    return Transform3f::Identity();
}
} // namespace

Transform3f PresentationPoseOf(const World& world, EntityId entity, double alpha)
{
    return PresentationPoseAt(world, entity, alpha, 0);
}

Transform3f ResolvePresentationPose(const WorldTransformHistory& history, double alpha)
{
    const float t = alpha <= 0.0 ? 0.0f : (alpha >= 1.0 ? 1.0f : static_cast<float>(alpha));
    return Transform3f::Interpolate(history.Previous, history.Current, t);
}
