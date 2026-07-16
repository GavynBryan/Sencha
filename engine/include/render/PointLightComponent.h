#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
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

template <>
struct TypeSchema<PointLightComponent>
{
    static constexpr std::string_view Name = "PointLight";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('P', 'L', 'G', 'T');

    static auto Fields()
    {
        const PointLightComponent defaults;
        return std::tuple{
            MakeField("color", &PointLightComponent::Color)
                .AsColor()
                .Default(defaults.Color),
            MakeField("intensity", &PointLightComponent::Intensity)
                .Default(defaults.Intensity),
            MakeField("range", &PointLightComponent::Range)
                .Default(defaults.Range),
            MakeField("enabled", &PointLightComponent::Enabled)
                .Default(defaults.Enabled),
            MakeField("cast_shadows", &PointLightComponent::CastShadows)
                .Default(defaults.CastShadows),
            MakeField("shadow_resolution", &PointLightComponent::ShadowResolution),
            MakeField("shadow_update", &PointLightComponent::ShadowUpdate),
            MakeField("shadow_softness", &PointLightComponent::ShadowSoftness)
                .Default(defaults.ShadowSoftness),
            MakeField("shadow_bias_scale", &PointLightComponent::ShadowBiasScale)
                .Default(defaults.ShadowBiasScale),
            MakeField("bake_contribution", &PointLightComponent::BakeContribution),
        };
    }
};
