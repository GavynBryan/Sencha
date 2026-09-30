#pragma once

#include <project/CookProfile.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace DocumentCookStepIds
{
    inline constexpr std::string_view BrushCells = "brush_cells";
    inline constexpr std::string_view LightmapSurfaces = "lightmap_surfaces";
    inline constexpr std::string_view OcclusionGeometry = "occlusion_geometry";
    inline constexpr std::string_view CookedScene = "cooked_scene";
    inline constexpr std::string_view Publication = "publication";
}

struct CookStepDefinition
{
    std::string_view Id;
    std::span<const std::string_view> Dependencies;
    std::string_view OutputFamily;
    std::uint32_t Version = 1;
    bool ProfileSelectable = true;
};

struct ResolvedCookGraph
{
    bool Valid = false;
    std::string Error;
    std::vector<std::string> OrderedSteps;
    std::vector<std::string> AddedPrerequisites;
};

[[nodiscard]] std::span<const CookStepDefinition> DocumentCookSteps();
[[nodiscard]] const CookStepDefinition* FindDocumentCookStep(std::string_view id);
[[nodiscard]] ResolvedCookGraph ResolveDocumentCookGraph(const CookProfile& profile);
[[nodiscard]] CookOutputDisposition CookProfileOutputDisposition(
    const CookProfile& profile, std::string_view family);
