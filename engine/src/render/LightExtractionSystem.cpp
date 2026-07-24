#include <render/LightExtractionSystem.h>

#include <math/geometry/3d/Sphere.h>
#include <render/LightSelection.h>
#include <render/RenderEntityKey.h>

#include <vector>

namespace
{
// One World means one entity namespace: generational EntityId is the stable
// ordering identity, and the registry-era key fields stay at their defaults.
RenderEntityKey MakeWorldEntityKey(EntityId entity)
{
    return RenderEntityKey{ .Entity = entity };
}
} // namespace

void LightExtractionSystem::Extract(
    const World& world,
    const StoragePartitionSet& partitions,
    const CameraRenderData& camera,
    RenderLightSet& lights,
    std::vector<SpotShadowRequest>& shadowRequests,
    std::vector<PointShadowRequest>& pointShadowRequests,
    LightExtractionCounts* counts)
{
    if (LastWorld != &world)
    {
        PointQuery.reset();
        SpotQuery.reset();
        LastWorld = &world;
    }

    std::vector<ForwardLightCandidate> candidates;

    if (world.IsRegistered<WorldTransform>()
        && world.IsRegistered<PointLightComponent>())
    {
        if (!PointQuery.has_value())
            PointQuery.emplace(world);

        PointQuery->ForEachChunkIn(partitions, [&](auto& view)
        {
            const auto transforms = view.template Read<WorldTransform>();
            const auto pointLights = view.template Read<PointLightComponent>();
            for (uint32_t i = 0; i < view.Count(); ++i)
            {
                const PointLightComponent& light = pointLights[i];
                if (!light.Enabled
                    || !IsUsableForwardLight(light.Intensity, light.Range))
                {
                    continue;
                }
                // Baked-direct lights contribute only through the zone
                // lightmap; they never enter the runtime forward set.
                if (light.BakeContribution == LightBakeContribution::Direct)
                    continue;

                const Vec<3>& position = transforms[i].Value.Position;
                const Sphere bounds(position, light.Range);
                if (!camera.ViewFrustum.IntersectsSphere(bounds))
                    continue;

                candidates.push_back(MakePointLightCandidate(
                    MakeWorldEntityKey(view.Entity(i)), position,
                    light, lights.ShadowSoftness));
            }
        });
    }

    if (world.IsRegistered<WorldTransform>()
        && world.IsRegistered<SpotLightComponent>())
    {
        if (!SpotQuery.has_value())
            SpotQuery.emplace(world);

        SpotQuery->ForEachChunkIn(partitions, [&](auto& view)
        {
            const auto transforms = view.template Read<WorldTransform>();
            const auto spotLights = view.template Read<SpotLightComponent>();
            for (uint32_t i = 0; i < view.Count(); ++i)
            {
                const SpotLightComponent& light = spotLights[i];
                if (!light.Enabled
                    || !IsUsableForwardLight(light.Intensity, light.Range))
                {
                    continue;
                }
                // Baked-direct lights contribute only through the zone
                // lightmap; they never enter the runtime forward set.
                if (light.BakeContribution == LightBakeContribution::Direct)
                    continue;

                const Vec<3>& position = transforms[i].Value.Position;
                const Vec<3> direction = transforms[i].Value.Forward();
                const Sphere bounds = MakeSpotBoundingSphere(
                    position, direction, light.Range, light.OuterAngleDegrees);
                if (!camera.ViewFrustum.IntersectsSphere(bounds))
                    continue;

                candidates.push_back(MakeSpotLightCandidate(
                    MakeWorldEntityKey(view.Entity(i)), transforms[i].Value,
                    light, lights.ShadowSoftness));
            }
        });
    }

    ForwardLightSelectionCounts selectionCounts;
    SelectForwardLights(candidates, camera.Position, lights, shadowRequests,
                        pointShadowRequests, &selectionCounts);
    if (counts != nullptr)
    {
        counts->FrustumCandidates = selectionCounts.Candidates;
        counts->Packed = selectionCounts.Packed;
    }
}

void LightExtractionSystem::Extract(
    const World& world,
    const CameraRenderData& camera,
    RenderLightSet& lights,
    std::vector<SpotShadowRequest>& shadowRequests,
    std::vector<PointShadowRequest>& pointShadowRequests,
    LightExtractionCounts* counts)
{
    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());
    for (EntityId entity : world.GetAliveEntities())
        partitions.Add(world.GetEntityPartition(entity));

    Extract(world, partitions, camera, lights, shadowRequests,
            pointShadowRequests, counts);
}
