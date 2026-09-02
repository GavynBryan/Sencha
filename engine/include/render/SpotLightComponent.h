#pragma once

#include <ecs/ComponentAnnotations.h>
#include <math/Vec.h>
#include <render/LightComponentTypes.h>

// A cone emitter whose direction comes from the entity's world-transform
// forward axis. Angles describe the full cone from the center line to the rim.
struct SENCHA_COMPONENT("SpotLight")
       SENCHA_SCHEMA("SpotLight")
       SENCHA_SCENE_CHUNK("SPOT")
SpotLightComponent
{
    SENCHA_FIELD("color")
    SENCHA_COLOR
    Vec<3> Color = Vec<3>(1.0f, 1.0f, 1.0f);

    SENCHA_FIELD("intensity")
    float Intensity = 1.0f;

    SENCHA_FIELD("range")
    float Range = 10.0f;

    SENCHA_FIELD("inner_angle_degrees")
    float InnerAngleDegrees = 25.0f;

    SENCHA_FIELD("outer_angle_degrees")
    float OuterAngleDegrees = 35.0f;

    SENCHA_FIELD("enabled")
    bool Enabled = true;

    SENCHA_FIELD("cast_shadows")
    SENCHA_TOOLTIP("Renders a realtime shadow map for this light. Costs one of "
                   "the frame's budgeted shadow slots.")
    bool CastShadows = false;

    SENCHA_FIELD("shadow_resolution")
    SENCHA_TOOLTIP("Requested shadow-map tile size: Low 256, Medium 512, High "
                   "1024.")
    ShadowResolutionTier ShadowResolution = ShadowResolutionTier::Medium;

    SENCHA_FIELD("shadow_update")
    SENCHA_LABEL("Shadow Update")
    SENCHA_TOOLTIP("How often this light's shadow map re-renders. Unrelated to "
                   "baked lighting.")
    ShadowUpdatePolicy ShadowUpdate = ShadowUpdatePolicy::OnChange;

    SENCHA_FIELD("shadow_softness")
    SENCHA_TOOLTIP("Widens the shadow filter, in texels, on top of the global "
                   "softness setting.")
    float ShadowSoftness = 1.5f;

    SENCHA_FIELD("shadow_bias_scale")
    SENCHA_TOOLTIP("Scales the depth bias that stops a surface shadowing "
                   "itself. Raise if lit surfaces stripe; lower if shadows "
                   "detach from their objects.")
    float ShadowBiasScale = 1.0f;

    SENCHA_FIELD("bake_contribution")
    SENCHA_LABEL("Lighting")
    SENCHA_TOOLTIP("How this light participates in baked lighting. Baking "
                   "happens when the zone cooks.")
    LightBakeContribution BakeContribution = LightBakeContribution::None;
};

#if !defined(SENCHA_CODEGEN)
#  include <render/SpotLightComponent.sencha.h>
#endif
