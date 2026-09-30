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
bool BakesDirectAs(const World& components, EntityId entity)
{
    if (!components.IsRegistered<T>())
        return false;
    const T* light = components.TryGet<T>(entity);
    return light != nullptr && light->BakeContribution == LightBakeContribution::Direct;
}
}

bool LightingReadModel::BakesDirect(const World& components, EntityId entity)
{
    return BakesDirectAs<PointLightComponent>(components, entity) || BakesDirectAs<SpotLightComponent>(components, entity);
}

bool LightingReadModel::IsIrradianceVolume(const World& components, EntityId entity)
{
    return components.IsRegistered<IrradianceVolumeComponent>()
        && components.TryGet<IrradianceVolumeComponent>(entity) != nullptr;
}
