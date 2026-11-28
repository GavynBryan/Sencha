#include <world/transform/TransformHistory.h>

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <math/geometry/3d/RigidMapping.h>
#include <world/transform/PresentationOffset.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace
{
Transform3f CrossingOf(const WorldTransformHistory& history)
{
    return Transform3f{ history.CrossingTranslation, history.CrossingRotation, Vec3d::One() };
}

void Roll(WorldTransformHistory& history, const Transform3f& pose, bool snapAll)
{
    if (snapAll || history.State == HistoryState::Snapping)
    {
        history.Previous = pose;
        history.Current = pose;
        history.State = HistoryState::Steady;
        return;
    }
    history.Previous = history.State == HistoryState::Carrying ? CrossingOf(history) * history.Current : history.Current;
    history.Current = pose;
    history.State = history.State == HistoryState::Carrying ? HistoryState::Crossing : HistoryState::Steady;
}
} // namespace

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
            Roll(histories[row], worlds[row].Value, snapAll);
    });
}

void CarryWorldTransformHistory(WorldTransformHistory& history, const Transform3f& mapping, float tickFraction)
{
    const float fraction = std::clamp(tickFraction, 0.0f, 1.0f);
    switch (history.State)
    {
    case HistoryState::Snapping:
        return;
    case HistoryState::Carrying:
    {
        const Transform3f composed = mapping * CrossingOf(history);
        history.CrossingRotation = composed.Rotation;
        history.CrossingTranslation = composed.Position;
        history.CrossedAt = std::max(history.CrossedAt, fraction);
        return;
    }
    case HistoryState::Steady:
    case HistoryState::Crossing:
        history.CrossingRotation = mapping.Rotation;
        history.CrossingTranslation = mapping.Position;
        history.CrossedAt = fraction;
        history.State = HistoryState::Carrying;
        return;
    }
}

void RequestTransformHistorySnap(World& world, EntityId entity)
{
    if (!world.IsRegistered<WorldTransformHistory>())
        return;
    if (WorldTransformHistory* history = world.TryGet<WorldTransformHistory>(entity))
        history->State = HistoryState::Snapping;
}

void PlaceSpawnedRoot(World& world, EntityId root, const Vec3d& position, std::optional<Quatf> rotation)
{
    const auto place = [&](Transform3f& pose) {
        pose.Position = position;
        if (rotation)
            pose.Rotation = *rotation;
    };
    if (LocalTransform* local = world.TryGet<LocalTransform>(root))
        place(local->Value);
    if (WorldTransform* placed = world.TryGet<WorldTransform>(root))
        place(placed->Value);
    if (!world.HasComponent<WorldTransformHistory>(root))
        world.AddComponent<WorldTransformHistory>(root, WorldTransformHistory{});
    RequestTransformHistorySnap(world, root);
}

namespace
{
// A chain this deep is a cycle, which propagation refuses to order.
constexpr int kMaxPresentationDepth = 64;

Transform3f InheritedPresentationOffset(const World& world, EntityId child, EntityId parent)
{
    if (!world.IsRegistered<InheritsPresentationOffset>() || !world.IsRegistered<PresentationOffset>()
        || !world.HasComponent<InheritsPresentationOffset>(child))
    {
        return Transform3f::Identity();
    }
    const PresentationOffset* offset = world.TryGet<PresentationOffset>(parent);
    return offset != nullptr ? offset->Value : Transform3f::Identity();
}

Transform3f PresentationPoseAt(const World& world, EntityId entity, double alpha, int depth)
{
    if (!entity.IsValid() || !world.IsAlive(entity))
        return Transform3f::Identity();
    if (world.IsRegistered<WorldTransformHistory>())
    {
        if (const WorldTransformHistory* history = world.TryGet<WorldTransformHistory>(entity))
            return ResolvePresentationPose(*history, alpha);
    }
    // Composed from the inputs: WorldTransform holds whichever domain swept it last.
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
        return PresentationPoseAt(world, parent->Entity, alpha, depth + 1)
               * InheritedPresentationOffset(world, entity, parent->Entity) * local->Value;
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
    const Transform3f blended = Transform3f::Interpolate(history.Previous, history.Current, t);
    // Only a frame drawn between an out-of-tick relocation and the next capture sees Carrying.
    if (history.State == HistoryState::Carrying)
        return CrossingOf(history) * blended;
    if (history.State == HistoryState::Crossing && t < history.CrossedAt)
        return InverseRigidMapping(CrossingOf(history)) * blended;
    return blended;
}
