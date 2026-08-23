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
        EnumValue{ ShadowUpdatePolicy::EveryFrame, "every_frame",
                   "Every Frame",
                   "Re-renders the shadow map every frame. For lights over "
                   "constantly changing scenes." },
        EnumValue{ ShadowUpdatePolicy::OnChange, "on_change",
                   "On Change",
                   "Re-renders when something inside the light's reach moves "
                   "or changes; cached otherwise. The usual choice." },
        EnumValue{ ShadowUpdatePolicy::Static, "static",
                   "Cached (renders once)",
                   "Renders once when the light gains a shadow slot, then "
                   "reuses the cached map. Cheapest; for lights over scenery "
                   "that never moves. Unrelated to baked lighting." },
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
        EnumValue{ LightBakeContribution::None, "none",
                   "Realtime",
                   "Fully dynamic: lit per frame, contributes nothing to "
                   "bakes." },
        EnumValue{ LightBakeContribution::Indirect, "indirect",
                   "Mixed (realtime light, baked bounce)",
                   "The light itself stays realtime; its bounce bakes into "
                   "irradiance probes when the zone cooks. Needs a probe "
                   "volume in the zone -- without one there is no bounce and "
                   "the light is simply realtime." },
        EnumValue{ LightBakeContribution::Direct, "direct",
                   "Baked",
                   "Direct light bakes into the zone's lightmap when the "
                   "zone cooks. The world gets this light from the lightmap; "
                   "moving objects still receive it live (it never displaces "
                   "realtime lights and never casts realtime shadows). "
                   "Bounce reaches probes where the zone has probe volumes." },
    };
};
