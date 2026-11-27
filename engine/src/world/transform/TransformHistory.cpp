#include <world/transform/TransformHistory.h>

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <world/transform/PresentationOffset.h>
#include <world/transform/TransformComponents.h>

#include <math/geometry/3d/RigidMapping.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace
{
// Every transition of a history at a capture, in one place.
void Roll(WorldTransformHistory& history, const Transform3f& pose, bool snap)
{
    if (snap)
    {
        // Both ends of the blend are this tick: drawn exactly where the simulation put it.
        history.Previous = pose;
        history.Current = pose;
        history.Snap = false;
        history.Crossing = HistoryCrossing::None;
        return;
    }
    if (history.Crossing == HistoryCrossing::Pending)
    {
        history.Previous = history.CrossingMapping * history.Current;
        history.Current = pose;
        history.Crossing = HistoryCrossing::Presenting;
        return;
    }
    history.Previous = history.Current;
    history.Current = pose;
    history.Crossing = HistoryCrossing::None;
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
            Roll(histories[row], worlds[row].Value, snapAll || histories[row].Snap);
    });
}

void CarryWorldTransformHistory(WorldTransformHistory& history, const Transform3f& mapping, float tickFraction)
{
    history.CrossingMapping =
        history.Crossing == HistoryCrossing::Pending ? mapping * history.CrossingMapping : mapping;
    history.CrossedAt = std::clamp(tickFraction, 0.0f, 1.0f);
    history.Crossing = HistoryCrossing::Pending;
}

void RequestTransformHistorySnap(World& world, EntityId entity)
{
    if (!world.IsRegistered<WorldTransformHistory>())
        return;
    if (WorldTransformHistory* history = world.TryGet<WorldTransformHistory>(entity))
        history->Snap = true;
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
    if (history.Crossing != HistoryCrossing::Presenting || t >= history.CrossedAt)
        return blended;
    return InverseRigidMapping(history.CrossingMapping) * blended;
}
