#pragma once

#include <core/metadata/EnumSchema.h>

#include <array>
#include <cstdint>
#include <string>

enum class LevelKind : std::uint8_t
{
    Scene,
    World,
};

template<>
struct EnumSchema<LevelKind>
{
    static constexpr std::array Values = {
        EnumValue{ LevelKind::Scene, "scene", "Scene" },
        EnumValue{ LevelKind::World, "world", "World" },
    };
};

struct LevelReference
{
    LevelKind Kind = LevelKind::Scene;
    std::string Name;

    friend bool operator==(const LevelReference&, const LevelReference&) = default;
};
