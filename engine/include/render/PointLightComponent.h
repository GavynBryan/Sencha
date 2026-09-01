#pragma once

#include <ecs/ComponentTypeId.h>
#include <math/Vec.h>
#include <render/LightComponentTypes.h>

#include <cstdint>
#include <string_view>
#include <tuple>

// An omnidirectional emitter at the entity's world position. Range is the
// attenuation cutoff in world units.
struct PointLightComponent
{
    Vec<3> Color = Vec<3>(1.0f, 1.0f, 1.0f);
    float Intensity = 1.0f;
    float Range = 10.0f;
    bool Enabled = true;

    bool CastShadows = false;
    ShadowResolutionTier ShadowResolution = ShadowResolutionTier::Medium;
    ShadowUpdatePolicy ShadowUpdate = ShadowUpdatePolicy::OnChange;
    float ShadowSoftness = 1.5f;
    float ShadowBiasScale = 1.0f;
    LightBakeContribution BakeContribution = LightBakeContribution::None;
};

SENCHA_DECLARE_COMPONENT_TYPE(PointLightComponent, "PointLight");
SENCHA_COMPONENT_DECLARES_SCHEMA(PointLightComponent);
