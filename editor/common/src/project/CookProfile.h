#pragma once

#include <core/json/JsonValue.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CookStepIds
{
    inline constexpr std::string_view RenderMeshes = "render_meshes";
    inline constexpr std::string_view Collision = "collision";
    inline constexpr std::string_view ReferencedAssets = "referenced_assets";
    inline constexpr std::string_view DirectLightmap = "direct_lightmap";
    inline constexpr std::string_view AmbientOcclusion = "ambient_occlusion";
    inline constexpr std::string_view IrradianceProbes = "irradiance_probes";
}

namespace CookOutputFamilies
{
    inline constexpr std::string_view Structure = "structure";
    inline constexpr std::string_view Collision = "collision";
    inline constexpr std::string_view ReferencedAssets = "referenced_assets";
    inline constexpr std::string_view DirectLightmap = "direct_lightmap";
    inline constexpr std::string_view AmbientOcclusion = "ambient_occlusion";
    inline constexpr std::string_view IrradianceProbes = "irradiance_probes";
}

enum class CookOutputDisposition
{
    Publish,
    Preserve,
    Withdraw,
};

struct CookOutputPolicy
{
    std::string Family;
    CookOutputDisposition Disposition = CookOutputDisposition::Preserve;
};

// A project-owned cook recipe. TargetSteps chooses work to refresh while
// OutputPolicies independently controls what the next published scene exposes.
// Stable string ids keep the project file forward-readable by editor versions
// that do not implement every step yet.
struct CookProfile
{
    std::string Id{};
    std::string Name{};
    std::vector<std::string> TargetSteps{};
    std::vector<CookOutputPolicy> OutputPolicies{};
    bool BuiltIn = false;
};

[[nodiscard]] std::span<const CookProfile> BuiltInCookProfiles();
[[nodiscard]] std::vector<CookProfile> ResolveCookProfiles(
    std::span<const CookProfile> projectProfiles);

[[nodiscard]] JsonValue WriteCookProfiles(std::span<const CookProfile> profiles);
[[nodiscard]] bool ReadCookProfiles(const JsonValue& value,
                                    std::vector<CookProfile>& out,
                                    std::string* error = nullptr);

