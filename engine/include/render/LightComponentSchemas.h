#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <math/Vec.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape for the light and probe components.
//
// Pure authoring: these components own nothing, so this unit needs neither the
// World nor a cache -- which is exactly why it is not filed with the meshes.
//=============================================================================

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
                .Default(defaults.CastShadows)
                .Tooltip("Renders a realtime shadow map for this light. "
                         "Costs one of the frame's budgeted shadow slots."),
            MakeField("shadow_resolution", &PointLightComponent::ShadowResolution)
                .Default(defaults.ShadowResolution)
                .Tooltip("Requested shadow-map tile size: Low 256, Medium "
                         "512, High 1024. Spot lights only -- point-light "
                         "cube faces are fixed at 512."),
            MakeField("shadow_update", &PointLightComponent::ShadowUpdate)
                .Default(defaults.ShadowUpdate)
                .Label("Shadow Update")
                .Tooltip("How often this light's shadow map re-renders. "
                         "Unrelated to baked lighting."),
            MakeField("shadow_softness", &PointLightComponent::ShadowSoftness)
                .Default(defaults.ShadowSoftness)
                .Tooltip("Widens the shadow filter, in texels, on top of the "
                         "global softness setting."),
            MakeField("shadow_bias_scale", &PointLightComponent::ShadowBiasScale)
                .Default(defaults.ShadowBiasScale)
                .Tooltip("Scales the depth bias that stops a surface "
                         "shadowing itself. Raise if lit surfaces stripe; "
                         "lower if shadows detach from their objects."),
            MakeField("bake_contribution", &PointLightComponent::BakeContribution)
                .Default(defaults.BakeContribution)
                .Label("Lighting")
                .Tooltip("How this light participates in baked lighting. "
                         "Baking happens when the zone cooks."),
        };
    }
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
                .Default(defaults.CastShadows)
                .Tooltip("Renders a realtime shadow map for this light. "
                         "Costs one of the frame's budgeted shadow slots."),
            MakeField("shadow_resolution", &SpotLightComponent::ShadowResolution)
                .Default(defaults.ShadowResolution)
                .Tooltip("Requested shadow-map tile size: Low 256, Medium "
                         "512, High 1024."),
            MakeField("shadow_update", &SpotLightComponent::ShadowUpdate)
                .Default(defaults.ShadowUpdate)
                .Label("Shadow Update")
                .Tooltip("How often this light's shadow map re-renders. "
                         "Unrelated to baked lighting."),
            MakeField("shadow_softness", &SpotLightComponent::ShadowSoftness)
                .Default(defaults.ShadowSoftness)
                .Tooltip("Widens the shadow filter, in texels, on top of the "
                         "global softness setting."),
            MakeField("shadow_bias_scale", &SpotLightComponent::ShadowBiasScale)
                .Default(defaults.ShadowBiasScale)
                .Tooltip("Scales the depth bias that stops a surface "
                         "shadowing itself. Raise if lit surfaces stripe; "
                         "lower if shadows detach from their objects."),
            MakeField("bake_contribution", &SpotLightComponent::BakeContribution)
                .Default(defaults.BakeContribution)
                .Label("Lighting")
                .Tooltip("How this light participates in baked lighting. "
                         "Baking happens when the zone cooks."),
        };
    }
};

template <>
struct TypeSchema<IrradianceVolumeComponent>
{
    static constexpr std::string_view Name = "IrradianceVolume";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('I', 'R', 'V', 'L');

    static auto Fields()
    {
        const IrradianceVolumeComponent defaults{};
        return std::tuple{
            MakeField("half_extents", &IrradianceVolumeComponent::HalfExtents)
                .Default(defaults.HalfExtents),
            MakeField("cell_size", &IrradianceVolumeComponent::CellSize)
                .Default(defaults.CellSize),
            MakeField("priority", &IrradianceVolumeComponent::Priority)
                .Default(defaults.Priority),
        };
    }
};
