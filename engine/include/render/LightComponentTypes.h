#pragma once

#include <core/metadata/EnumSchema.h>

#include <array>
#include <cstdint>

enum class ShadowResolutionTier : std::uint16_t
{
    Low = 256,
    Medium = 512,
    High = 1024,
};

template <>
struct EnumSchema<ShadowResolutionTier>
{
    static constexpr std::array Values = {
        EnumValue{ ShadowResolutionTier::Low, "low" },
        EnumValue{ ShadowResolutionTier::Medium, "medium" },
        EnumValue{ ShadowResolutionTier::High, "high" },
    };
};

enum class ShadowUpdatePolicy : std::uint8_t
{
    EveryFrame,
    OnChange,
    Static,
};

template <>
struct EnumSchema<ShadowUpdatePolicy>
{
    static constexpr std::array Values = {
        EnumValue{ ShadowUpdatePolicy::EveryFrame, "every_frame" },
        EnumValue{ ShadowUpdatePolicy::OnChange, "on_change" },
        EnumValue{ ShadowUpdatePolicy::Static, "static" },
    };
};

enum class LightBakeContribution : std::uint8_t
{
    None,
    // Direct lighting stays dynamic (a runtime light like None), but the
    // light's bounce feeds the irradiance-probe bake, so its mood reaches
    // probe-lit ambient without paying for baked direct.
    Indirect,
    // The light's direct diffuse is baked into the zone's lightmap atlas. At
    // runtime the light stays in the forward set flagged baked, ranked below
    // every live light (it can fill empty cap slots but never evict live
    // light) and never requesting a shadow slot: receivers that own a chart
    // skip it in-shader because their copy is in the lightmap, while movable
    // and uncharted receivers are lit by it live. For static fill/accent
    // lights whose rooms must still light what walks through them.
    Direct,
};

template <>
struct EnumSchema<LightBakeContribution>
{
    static constexpr std::array Values = {
        EnumValue{ LightBakeContribution::None, "none" },
        EnumValue{ LightBakeContribution::Indirect, "indirect" },
        EnumValue{ LightBakeContribution::Direct, "direct" },
    };
};
