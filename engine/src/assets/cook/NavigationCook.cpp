#include <assets/cook/NavigationCook.h>

#include <navigation/NavLinkComponent.h>
#include <navigation/NavTileMesh.h>
#include <navigation/NavigationFile.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace
{
    // Tile references are budgeted; a zone needing more tiles than this is a
    // partitioning mistake, not content.
    constexpr std::int64_t kMaxZoneTiles = 1 << 16;

    void AddError(std::vector<CookDiagnostic>& diagnostics, CookDiagnosticSource source,
                  std::uint64_t id, std::string rule, std::string message)
    {
        diagnostics.push_back(CookDiagnostic{ CookDiagnosticSeverity::Error, source, id,
                                              std::move(rule), std::move(message) });
    }

    void AddLinkError(std::vector<CookDiagnostic>& diagnostics, NavLinkId link, std::string rule,
                      std::string_view message)
    {
        AddError(diagnostics, CookDiagnosticSource::NavLink, link.Value, std::move(rule),
                 std::format("navigation link {}: {}", NavLinkIdToString(link), message));
    }

    bool Finite(const Vec3d& v)
    {
        return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
    }

    // Authored-field checks that need no navmesh. False when the link cannot
    // be cooked at all.
    bool ValidateLink(const NavLinkRecord& link, std::set<std::uint64_t>& seenIds,
                      std::vector<CookDiagnostic>& diagnostics)
    {
        bool valid = true;
        const auto fail = [&](std::string rule, std::string_view message)
        {
            AddLinkError(diagnostics, link.Id, std::move(rule), message);
            valid = false;
        };
        if (!link.Id.IsValid())
            fail("nav.link.id_invalid", "has no identity");
        else if (!seenIds.insert(link.Id.Value).second)
            fail("nav.link.id_duplicate", "identity is used by another link");
        if (!Finite(link.Entry) || !Finite(link.Exit))
            fail("nav.link.anchor_nonfinite", "an anchor is not finite");
        else if ((link.Exit - link.Entry).Magnitude() < 1e-3f)
            fail("nav.link.degenerate", "entry and exit coincide");
        if (link.Directions == 0 || (link.Directions & ~NavLinkDirectionBoth) != 0)
            fail("nav.link.direction_invalid", "direction bits are invalid");
        if (link.Traversal.empty())
            fail("nav.link.traversal_empty", "names no traversal kind");
        if (!std::isfinite(link.BaseCost) || link.BaseCost < 0.0f
            || !std::isfinite(link.EntryRadius) || link.EntryRadius <= 0.0f)
            fail("nav.link.cost_invalid", "base cost or entry radius is out of range");
        return valid;
    }

    // Valid links in id order, so the file and diagnostics are canonical
    // whatever order the entities were collected in.
    std::vector<NavLinkRecord> ValidLinksInIdOrder(std::span<const NavLinkRecord> input,
                                                   std::vector<CookDiagnostic>& diagnostics)
    {
        std::vector<NavLinkRecord> links(input.begin(), input.end());
        std::ranges::sort(links, {}, [](const NavLinkRecord& link) { return link.Id.Value; });
        std::set<std::uint64_t> seenIds;
        std::erase_if(links, [&](const NavLinkRecord& link)
                      { return !ValidateLink(link, seenIds, diagnostics); });
        return links;
    }

    // Builds every tile of one profile that static geometry reaches, recording
    // each tile's triangle list for runtime rebuilds, and loads them into `mesh`.
    std::vector<NavTileRecord> BuildProfileTiles(const NavigationFile& file,
                                                 const NavigationProfileSetting& setting,
                                                 std::span<const NavAreaVolume> areas,
                                                 const Aabb3d& bounds, NavTileMesh& mesh,
                                                 std::vector<CookDiagnostic>& diagnostics)
    {
        const NavBuildProfile& profile = setting.Build;
        const NavTileRange range = NavTilesTouching(profile, bounds);
        const std::int64_t tileCount = static_cast<std::int64_t>(range.Max.X - range.Min.X + 1)
                                     * (range.Max.Z - range.Min.Z + 1);
        if (tileCount > kMaxZoneTiles)
        {
            AddError(diagnostics, CookDiagnosticSource::Document, 0, "nav.zone.too_large",
                     std::format("profile '{}' needs {} tiles; raise tile_cells or split the zone",
                                 setting.Name, tileCount));
            return {};
        }
        (void)mesh.Init(profile, static_cast<std::uint32_t>(tileCount));

        std::vector<NavTileRecord> tiles;
        std::vector<Vec3d> tilePositions;
        std::vector<std::uint32_t> tileIndices;
        const std::uint32_t triangleCount = static_cast<std::uint32_t>(file.Indices.size() / 3);
        for (std::int32_t z = range.Min.Z; z <= range.Max.Z; ++z)
            for (std::int32_t x = range.Min.X; x <= range.Max.X; ++x)
            {
                NavTileRecord tile;
                tile.Coord = NavTileCoord{ x, z };
                const Aabb3d tileBounds =
                    NavTileBuildBounds(profile, tile.Coord, file.MinY, file.MaxY);
                tilePositions.clear();
                tileIndices.clear();
                for (std::uint32_t t = 0; t < triangleCount; ++t)
                {
                    const Vec3d& a = file.Positions[file.Indices[t * 3 + 0]];
                    const Vec3d& b = file.Positions[file.Indices[t * 3 + 1]];
                    const Vec3d& c = file.Positions[file.Indices[t * 3 + 2]];
                    if (!NavTriangleTouchesTile(tileBounds, a, b, c))
                        continue;
                    tile.Triangles.push_back(t);
                    const std::uint32_t base = static_cast<std::uint32_t>(tilePositions.size());
                    tilePositions.insert(tilePositions.end(), { a, b, c });
                    tileIndices.insert(tileIndices.end(), { base, base + 1, base + 2 });
                }
                if (tile.Triangles.empty())
                    continue;

                NavTileBuildInput build{ profile, tile.Coord, tilePositions, tileIndices, areas,
                                         file.MinY, file.MaxY };
                if (BuildNavTile(build, tile.Data) == NavTileBuildResult::Failed)
                    AddError(diagnostics, CookDiagnosticSource::Document, 0, "nav.tile.build_failed",
                             std::format("profile '{}' tile ({}, {}) failed to build",
                                         setting.Name, x, z));
                else if (!tile.Data.empty())
                    (void)mesh.SetTile(tile.Coord, tile.Data);
                tiles.push_back(std::move(tile));
            }
        return tiles;
    }

    void CheckLinkAnchors(std::span<const NavLinkRecord> links, const NavTileMesh& mesh,
                          std::string_view profileName, std::vector<CookDiagnostic>& diagnostics)
    {
        for (const NavLinkRecord& link : links)
        {
            const auto check = [&](const Vec3d& anchor, const char* rule, const char* which)
            {
                if (!mesh.AttachLinkAnchor(anchor, link.EntryRadius))
                    AddLinkError(diagnostics, link.Id, rule,
                                 std::format("{} does not reach walkable space for profile '{}'",
                                             which, profileName));
            };
            check(link.Entry, "nav.link.entry_unprojected", "entry");
            check(link.Exit, "nav.link.exit_unprojected", "exit");
        }
    }

    void CheckAreaVolumes(std::span<const NavAreaVolume> volumes, std::size_t authoredAreas,
                          std::vector<CookDiagnostic>& diagnostics)
    {
        for (const NavAreaVolume& volume : volumes)
            if (volume.Area == kNavDefaultArea || volume.Area > authoredAreas)
                AddError(diagnostics, CookDiagnosticSource::Document, 0, "nav.area.unknown",
                         std::format("area volume names area index {}, which the settings "
                                     "do not list", volume.Area));
    }
}

NavigationCookResult CookZoneNavigation(const NavigationCookInput& input)
{
    NavigationCookResult result;
    std::vector<CookDiagnostic>& diagnostics = result.Diagnostics;
    const NavigationSettings* settings = input.Settings;
    if (settings == nullptr || settings->Profiles.empty())
    {
        AddError(diagnostics, CookDiagnosticSource::NavigationSettings, 0, "nav.settings.invalid",
                 "navigation cook has no build profiles");
        return result;
    }
    CheckAreaVolumes(input.AreaVolumes, settings->Areas.size(), diagnostics);

    NavigationFile file;
    file.Areas.push_back(std::string(kNavigationDefaultAreaName));
    file.Areas.insert(file.Areas.end(), settings->Areas.begin(), settings->Areas.end());
    file.Positions.assign(input.Positions.begin(), input.Positions.end());
    file.Indices.assign(input.Indices.begin(), input.Indices.end());
    file.Links = ValidLinksInIdOrder(input.Links, diagnostics);

    Aabb3d bounds = Aabb3d::Empty();
    for (const Vec3d& position : file.Positions)
        bounds.ExpandToInclude(position);
    const bool hasGeometry = file.Indices.size() >= 3 && bounds.IsValid();
    if (hasGeometry)
    {
        const auto tallest = std::ranges::max(settings->Profiles, {},
            [](const NavigationProfileSetting& p) { return p.Build.Height; });
        file.MinY = bounds.Min.Y - 1.0f;
        file.MaxY = bounds.Max.Y + tallest.Build.Height + 1.0f;
    }

    for (const NavigationProfileSetting& setting : settings->Profiles)
    {
        NavProfileRecord& record = file.Profiles.emplace_back();
        record.Name = setting.Name;
        record.Build = setting.Build;
        if (!hasGeometry)
            continue;
        NavTileMesh mesh;
        record.Tiles = BuildProfileTiles(file, setting, input.AreaVolumes, bounds, mesh,
                                         diagnostics);
        result.TileCount += mesh.TileCount();
        result.PolygonCount += mesh.PolygonCount();
        CheckLinkAnchors(file.Links, mesh, setting.Name, diagnostics);
    }

    if (hasGeometry && !result.HasErrors())
        result.Navigation = std::move(file);
    return result;
}

bool NavigationCookResult::HasErrors() const
{
    return HasCookErrors(Diagnostics);
}
