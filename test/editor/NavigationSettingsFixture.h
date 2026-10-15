#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>

// A project's navigation.settings with one humanoid profile, written under a
// content root so a level there builds its navigation in memory.
inline constexpr std::string_view kTestNavigationSettings = R"({
    "type": "navigation.settings", "version": 1,
    "data": {
        "profiles": [ { "tag": "navigation.profile.humanoid", "radius": 0.3,
                        "height": 1.8, "max_slope_degrees": 45, "max_climb": 0.35,
                        "cell_size": 0.2, "cell_height": 0.1, "tile_cells": 40 } ],
        "areas": [] } })";

inline void WriteTestNavigationSettings(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root / "data");
    std::ofstream(root / "data/navigation.sdata", std::ios::trunc) << kTestNavigationSettings;
}
