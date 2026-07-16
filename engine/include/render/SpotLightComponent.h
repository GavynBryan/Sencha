#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
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

template <>
struct TypeSchema<SpotLightComponent>
{
    static constexpr std::string_view Name = "SpotLight";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'P', 'O', 'T');

    static auto Fields()
    {
        const SpotLightComponent defaults;
        return std::tuple{
            MakeField("color", &SpotLightComponent::Color)
                .AsColor()
                .Default(defaults.Color),
            MakeField("intensity", &SpotLightComponent::Intensity)
                .Default(defaults.Intensity),
            MakeField("range", &SpotLightComponent::Range)
                .Default(defaults.Range),
            MakeField("inner_angle_degrees", &SpotLightComponent::InnerAngleDegrees)
                .Default(defaults.InnerAngleDegrees),
            MakeField("outer_angle_degrees", &SpotLightComponent::OuterAngleDegrees)
                .Default(defaults.OuterAngleDegrees),
            MakeField("enabled", &SpotLightComponent::Enabled)
                .Default(defaults.Enabled),
            MakeField("cast_shadows", &SpotLightComponent::CastShadows)
                .Default(defaults.CastShadows),
            MakeField("shadow_resolution", &SpotLightComponent::ShadowResolution),
            MakeField("shadow_update", &SpotLightComponent::ShadowUpdate),
            MakeField("shadow_softness", &SpotLightComponent::ShadowSoftness)
                .Default(defaults.ShadowSoftness),
            MakeField("shadow_bias_scale", &SpotLightComponent::ShadowBiasScale)
                .Default(defaults.ShadowBiasScale),
            MakeField("bake_contribution", &SpotLightComponent::BakeContribution),
        };
    }
};
