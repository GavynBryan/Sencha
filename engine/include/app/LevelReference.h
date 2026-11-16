#pragma once

#include <cstdint>
#include <string>

enum class LevelKind : std::uint8_t
{
    Scene,
    World,
};

struct LevelReference
{
    LevelKind Kind = LevelKind::Scene;
    std::string Name;

    friend bool operator==(const LevelReference&, const LevelReference&) = default;
};
