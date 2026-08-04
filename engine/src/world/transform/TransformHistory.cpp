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

Transform3f ResolvePresentationPose(const WorldTransformHistory& history, double alpha)
{
    const float t = alpha <= 0.0 ? 0.0f : (alpha >= 1.0 ? 1.0f : static_cast<float>(alpha));
    return Transform3f::Interpolate(history.Previous, history.Current, t);
}
