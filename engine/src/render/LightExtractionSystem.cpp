#include <render/LightExtractionSystem.h>

void LightExtractionSystem::Extract(const World& world, RenderLightSet& lights)
{
    if (!world.IsRegistered<WorldTransform>()
        || !world.IsRegistered<PointLightComponent>())
    {
        return;
    }

    if (LastWorld != &world || !CachedQuery.has_value())
    {
        CachedQuery.emplace(world);
        LastWorld = &world;
    }

    CachedQuery->ForEachChunk([&](auto& view)
    {
        const auto transforms = view.template Read<WorldTransform>();
        const auto pointLights = view.template Read<PointLightComponent>();
        const uint32_t count = view.Count();

        for (uint32_t i = 0; i < count; ++i)
        {
            const PointLightComponent& light = pointLights[i];
            if (!light.Enabled)
                continue;

            lights.AddPoint(transforms[i].Value.Position, light);
        }
    });
}
