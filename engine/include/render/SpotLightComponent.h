#pragma once

#include <ecs/ComponentTypeId.h>
#include <math/Vec.h>
#include <render/LightComponentTypes.h>

#include <cstdint>
#include <string_view>
#include <tuple>

// A cone emitter whose direction comes from the entity's world-transform
// forward axis. Angles describe the full cone from the center line to the rim.
struct SpotLightComponent
{
    Vec<3> Color = Vec<3>(1.0f, 1.0f, 1.0f);
    float Intensity = 1.0f;
    float Range = 10.0f;
    float InnerAngleDegrees = 25.0f;
    float OuterAngleDegrees = 35.0f;
    bool Enabled = true;

    bool CastShadows = false;
    ShadowResolutionTier ShadowResolution = ShadowResolutionTier::Medium;
    ShadowUpdatePolicy ShadowUpdate = ShadowUpdatePolicy::OnChange;
    float ShadowSoftness = 1.5f;
    float ShadowBiasScale = 1.0f;
    LightBakeContribution BakeContribution = LightBakeContribution::None;
};

SENCHA_DECLARE_COMPONENT_TYPE(SpotLightComponent, "SpotLight");
