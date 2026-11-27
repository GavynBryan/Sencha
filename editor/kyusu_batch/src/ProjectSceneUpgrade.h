#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>

enum class ProjectSceneUpgradeMode : std::uint8_t
{
    Check,
    Write,
};

struct ProjectSceneTally
{
    std::size_t Upgraded = 0;
    std::size_t Current = 0;
    std::size_t Old = 0;
    std::size_t Refused = 0;
};

// Every .sscene beneath `root`, hidden directories and CMake build trees aside, one line each.
[[nodiscard]] ProjectSceneTally UpgradeProjectScenes(const std::filesystem::path& root, ProjectSceneUpgradeMode mode,
                                                     std::ostream& out);
