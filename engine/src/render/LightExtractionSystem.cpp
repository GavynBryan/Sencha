#include <render/LightExtractionSystem.h>

#include <math/geometry/3d/Sphere.h>
#include <render/LightSelection.h>
#include <render/PointLightComponent.h>
#include <render/RenderEntityKey.h>
#include <render/SpotLightComponent.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <vector>

void LightExtractionSystem::Extract(std::span<Registry*> registries,
                                    const CameraRenderData& camera,
                                    RenderLightSet& lights,
                                    std::vector<SpotShadowRequest>& shadowRequests,
                                    std::vector<PointShadowRequest>& pointShadowRequests,
                                    LightExtractionCounts* counts) const
{
    std::vector<ForwardLightCandidate> candidates;

    for (Registry* registry : registries)
    {
        if (registry == nullptr)
            continue;

        const World& world = registry->Components;
        if (!world.IsRegistered<WorldTransform>())
            continue;

        if (world.IsRegistered<PointLightComponent>())
        {
            world.ForEachComponent<PointLightComponent>(
                [&](EntityId entity, const PointLightComponent& light)
                {
                    if (!light.Enabled
                        || !IsUsableForwardLight(light.Intensity, light.Range))
                        return;
                    // Baked-direct lights contribute only through the zone
                    // lightmap; they never enter the runtime forward set.
                    if (light.BakeContribution == LightBakeContribution::Direct)
                        return;

                    const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
                    if (transform == nullptr)
                        return;

                    const Vec<3>& position = transform->Value.Position;
                    const Sphere bounds(position, light.Range);
                    if (!camera.ViewFrustum.IntersectsSphere(bounds))
                        return;

                    candidates.push_back(MakePointLightCandidate(
                        MakeRenderEntityKey(*registry, entity), position,
                        light, lights.ShadowSoftness));
                });
        }

        if (world.IsRegistered<SpotLightComponent>())
        {
            world.ForEachComponent<SpotLightComponent>(
                [&](EntityId entity, const SpotLightComponent& light)
                {
                    if (!light.Enabled
                        || !IsUsableForwardLight(light.Intensity, light.Range))
                        return;
                    // Baked-direct lights contribute only through the zone
                    // lightmap; they never enter the runtime forward set.
                    if (light.BakeContribution == LightBakeContribution::Direct)
                        return;

                    const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
                    if (transform == nullptr)
                        return;

                    const Vec<3>& position = transform->Value.Position;
                    const Vec<3> direction = transform->Value.Forward();
                    const Sphere bounds = MakeSpotBoundingSphere(
                        position, direction, light.Range, light.OuterAngleDegrees);
                    if (!camera.ViewFrustum.IntersectsSphere(bounds))
                        return;

                    candidates.push_back(MakeSpotLightCandidate(
                        MakeRenderEntityKey(*registry, entity), transform->Value,
                        light, lights.ShadowSoftness));
                });
        }
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
