#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <math/Vec.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// PointLightComponent
//
// A scene-resident point light: an omni emitter at the entity's world
// position. Color is linear RGB; Intensity scales brightness; Range is the
// attenuation cutoff in world units (the falloff is windowed so the light
// contributes nothing past Range, keeping it local). Authored in scene JSON
// and gathered each frame by LightExtractionSystem into the forward pass.
//
// Point is the only light type today. Spot and directional lights are
// roadmapped; when they land they add their own component and feed the same
// per-frame light list, so this stays the omni case, not a tagged union.
//=============================================================================
struct PointLightComponent
{
    Vec<3> Color    = Vec<3>(1.0f, 1.0f, 1.0f); // linear RGB
    float  Intensity = 1.0f;                    // brightness multiplier
    float  Range     = 10.0f;                   // attenuation cutoff (world units)
    bool   Enabled   = true;
};

template <>
struct TypeSchema<PointLightComponent>
{
    static constexpr std::string_view Name = "PointLight";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('P', 'L', 'G', 'T');

    static auto Fields()
    {
        return std::tuple{
            MakeField("color", &PointLightComponent::Color)
                .AsColor()
                .Default(Vec<3>(1.0f, 1.0f, 1.0f)),
            MakeField("intensity", &PointLightComponent::Intensity).Default(1.0f),
            MakeField("range", &PointLightComponent::Range).Default(10.0f),
            MakeField("enabled", &PointLightComponent::Enabled).Default(true),
        };
    }
};
