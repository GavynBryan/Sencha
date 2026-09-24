#pragma once

#include <assets/cook/CookDiagnostic.h>
#include <assets/cook/NavigationSettings.h>
#include <math/Vec.h>
#include <navigation/NavTileBuild.h>
#include <navigation/NavigationFile.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct NavigationCookInput
{
    // The zone's static collision triangles, world space
    // (CollectStaticCollisionGeometry).
    std::span<const Vec3d> Positions;
    std::span<const std::uint32_t> Indices;
    const NavigationSettings* Settings = nullptr;
    // Area classification volumes; Area indexes Settings->Areas plus one. The
    // level cook passes none until an authored area source exists.
    std::span<const NavAreaVolume> AreaVolumes;
    // World-space anchors, resolved from each authoring entity's transform.
    std::span<const NavLinkRecord> Links;
};

struct NavigationCookResult
{
    // The zone's cooked navigation. Absent when the zone has no static geometry
    // or the cook reported an error. Persisting it (EncodeNavigationFile) is the
    // caller's concern.
    std::optional<NavigationFile> Navigation;
    std::vector<CookDiagnostic> Diagnostics;
    std::size_t TileCount = 0;
    std::size_t PolygonCount = 0;

    [[nodiscard]] bool HasErrors() const;
};

// Builds every profile's tiles for one zone and validates and attaches its
// links. Deterministic: identical input yields an identical result.
[[nodiscard]] NavigationCookResult CookZoneNavigation(const NavigationCookInput& input);
