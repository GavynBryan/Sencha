#include "LightingReadModel.h"

#include <ecs/World.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>

namespace
{
// A light type only participates when the document's world registered it, which
// depends on the loaded game module.
template <typename T>
std::uint32_t CountDirectBakeOf(const World& components)
{
    if (!components.IsRegistered<T>())
        return 0;
    std::uint32_t count = 0;
    components.ForEachComponent<T>(
        [&](EntityId, const T& light)
        {
            if (light.BakeContribution == LightBakeContribution::Direct)
                ++count;
        });
    return count;
}
}

std::uint32_t LightingReadModel::CountDirectBakeLights(const World& components)
{
    return CountDirectBakeOf<PointLightComponent>(components)
         + CountDirectBakeOf<SpotLightComponent>(components);
}

std::uint32_t LightingReadModel::CountAuthoredIrradianceVolumes(const World& components)
{
    if (!components.IsRegistered<IrradianceVolumeComponent>())
        return 0;
    std::uint32_t count = 0;
    components.ForEachComponent<IrradianceVolumeComponent>(
        [&](EntityId, const IrradianceVolumeComponent&) { ++count; });
    return count;
}
