#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>

#include <cstdint>
#include <string_view>

// A game-defined component: the example of how a game adds its own data. A
// schema (below) makes it serialize through the cook and show up, editable, in
// the editor inspector with no editor code naming it. SpinSystem (in
// TemplateGame.cpp) rotates entities that carry it. Replace this with your own
// components.
struct SpinComponent
{
    float RadiansPerSecond = 1.0f;
};

template <>
struct TypeSchema<SpinComponent>
{
    static constexpr std::string_view Name = "spin";
    // A unique tag identifying this component's chunk in binary scene data.
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'P', 'I', 'N');

    static auto Fields()
    {
        return std::tuple{
            MakeField("radians_per_second", &SpinComponent::RadiansPerSecond),
        };
    }
};
