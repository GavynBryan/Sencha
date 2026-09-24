#include "NavigationFixture.h"

#include <assets/cook/NavigationCook.h>
#include <core/json/JsonParser.h>
#include <navigation/NavLinkComponent.h>
#include <navigation/NavigationFile.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    NavigationSettings OneProfile()
    {
        NavigationSettings settings;
        settings.Profiles.push_back({ "navigation.profile.humanoid", TestProfile() });
        settings.Areas = { "navigation.area.water" };
        return settings;
    }

    NavLinkRecord Link(std::uint64_t id, Vec3d entry, Vec3d exit)
    {
        NavLinkRecord link;
        link.Id = NavLinkId{ id };
        link.Traversal = "navigation.traversal.jump";
        link.Directions = NavLinkDirectionForward;
        link.Entry = entry;
        link.Exit = exit;
        return link;
    }

    bool HasRule(const NavigationCookResult& result, std::string_view rule, std::uint64_t id)
    {
        return std::any_of(result.Diagnostics.begin(), result.Diagnostics.end(),
            [&](const CookDiagnostic& d) { return d.Rule == rule && d.SourceId == id; });
    }
}

TEST(NavigationCook, TwoFloorsCookAndSurviveTheFileCodec)
{
    NavTestGeometry geometry;
    geometry.AddFloor(0.0f, 0.0f, 10.0f, 10.0f);
    geometry.AddFloor(14.0f, 0.0f, 24.0f, 10.0f);
    const NavigationSettings settings = OneProfile();
    const NavLinkRecord jump = Link(0x11, Vec3d(9.0f, 0.0f, 5.0f),
                                         Vec3d(15.0f, 0.0f, 5.0f));

    NavigationCookInput input;
    input.Positions = geometry.Positions;
    input.Indices = geometry.Indices;
    input.Settings = &settings;
    input.Links = std::span<const NavLinkRecord>(&jump, 1);
    const NavigationCookResult result = CookZoneNavigation(input);
    EXPECT_TRUE(result.Diagnostics.empty());
    EXPECT_GT(result.TileCount, 0u);
    ASSERT_TRUE(result.Navigation.has_value());

    // What the cook produced survives the file codec unchanged.
    NavigationFile file;
    std::string error;
    ASSERT_TRUE(DecodeNavigationFile(EncodeNavigationFile(*result.Navigation), file, &error))
        << error;
    ASSERT_EQ(file.Profiles.size(), 1u);
    EXPECT_EQ(file.Profiles[0].Name, "navigation.profile.humanoid");
    EXPECT_EQ(file.Areas, (std::vector<std::string>{ "navigation.area.default",
                                                      "navigation.area.water" }));
    ASSERT_EQ(file.Links.size(), 1u);
    EXPECT_EQ(file.Links[0].Id, NavLinkId{ 0x11 });
    EXPECT_EQ(file.Positions.size(), geometry.Positions.size());

    // Every tile record lists its triangles in ascending order, and each
    // record with polygons loads into a mesh at its own coordinate.
    NavTileMesh mesh;
    ASSERT_TRUE(mesh.Init(file.Profiles[0].Build, 64));
    for (const NavTileRecord& tile : file.Profiles[0].Tiles)
    {
        EXPECT_TRUE(std::is_sorted(tile.Triangles.begin(), tile.Triangles.end()));
        if (!tile.Data.empty())
        {
            EXPECT_TRUE(mesh.SetTile(tile.Coord, tile.Data));
        }
    }
    EXPECT_EQ(mesh.TileCount(), result.TileCount);
}

TEST(NavigationCook, IdenticalInputProducesIdenticalFile)
{
    NavTestGeometry geometry;
    geometry.AddFloor(-5.0f, -5.0f, 20.0f, 12.0f);
    geometry.AddBox(Vec3d(4.0f, 0.0f, -5.0f), Vec3d(4.5f, 3.0f, 8.0f));
    const NavigationSettings settings = OneProfile();
    NavigationCookInput input;
    input.Positions = geometry.Positions;
    input.Indices = geometry.Indices;
    input.Settings = &settings;

    const NavigationCookResult first = CookZoneNavigation(input);
    const NavigationCookResult second = CookZoneNavigation(input);
    ASSERT_TRUE(first.Navigation.has_value());
    ASSERT_TRUE(second.Navigation.has_value());
    EXPECT_EQ(EncodeNavigationFile(*first.Navigation), EncodeNavigationFile(*second.Navigation));
}

TEST(NavigationCook, LinkDiagnosticsNameTheLink)
{
    NavTestGeometry geometry;
    geometry.AddFloor(0.0f, 0.0f, 10.0f, 10.0f);
    const NavigationSettings settings = OneProfile();

    std::vector<NavLinkRecord> links;
    links.push_back(Link(0, Vec3d(2.0f, 0.0f, 2.0f), Vec3d(4.0f, 0.0f, 2.0f)));
    links.push_back(Link(0x21, Vec3d(2.0f, 0.0f, 2.0f), Vec3d(4.0f, 0.0f, 2.0f)));
    links.push_back(Link(0x21, Vec3d(2.0f, 0.0f, 3.0f), Vec3d(4.0f, 0.0f, 3.0f)));
    links.push_back(Link(0x22, Vec3d(2.0f, 0.0f, 2.0f), Vec3d(2.0f, 0.0f, 2.0f)));
    links.push_back(Link(0x23, Vec3d(2.0f, 0.0f, 2.0f), Vec3d(4.0f, 0.0f, 2.0f)));
    links.back().Directions = 0;
    links.push_back(Link(0x24, Vec3d(2.0f, 0.0f, 2.0f), Vec3d(4.0f, 0.0f, 2.0f)));
    links.back().Traversal.clear();
    // The exit floats in the void, far from any floor.
    links.push_back(Link(0x25, Vec3d(2.0f, 0.0f, 2.0f), Vec3d(40.0f, 0.0f, 40.0f)));

    NavigationCookInput input;
    input.Positions = geometry.Positions;
    input.Indices = geometry.Indices;
    input.Settings = &settings;
    input.Links = links;
    const NavigationCookResult result = CookZoneNavigation(input);
    EXPECT_TRUE(result.HasErrors());
    EXPECT_FALSE(result.Navigation.has_value());
    EXPECT_TRUE(HasRule(result, "nav.link.id_invalid", 0));
    EXPECT_TRUE(HasRule(result, "nav.link.id_duplicate", 0x21));
    EXPECT_TRUE(HasRule(result, "nav.link.degenerate", 0x22));
    EXPECT_TRUE(HasRule(result, "nav.link.direction_invalid", 0x23));
    EXPECT_TRUE(HasRule(result, "nav.link.traversal_empty", 0x24));
    EXPECT_TRUE(HasRule(result, "nav.link.exit_unprojected", 0x25));
    EXPECT_FALSE(HasRule(result, "nav.link.entry_unprojected", 0x25));
}

TEST(NavigationCook, UnknownAreaIndexIsAnError)
{
    NavTestGeometry geometry;
    geometry.AddFloor(0.0f, 0.0f, 10.0f, 10.0f);
    const NavigationSettings settings = OneProfile();
    NavAreaVolume volume;
    volume.Footprint = { Vec3d(1, 0, 1), Vec3d(3, 0, 1), Vec3d(3, 0, 3) };
    volume.MinY = -1.0f;
    volume.MaxY = 1.0f;
    volume.Area = 5;
    NavigationCookInput input;
    input.Positions = geometry.Positions;
    input.Indices = geometry.Indices;
    input.Settings = &settings;
    input.AreaVolumes = std::span<const NavAreaVolume>(&volume, 1);
    const NavigationCookResult result = CookZoneNavigation(input);
    EXPECT_TRUE(result.HasErrors());
    EXPECT_TRUE(HasRule(result, "nav.area.unknown", 0));
}

TEST(NavigationFile, CorruptionAndVersionAreRejected)
{
    NavTestGeometry geometry;
    geometry.AddFloor(0.0f, 0.0f, 10.0f, 10.0f);
    const NavigationSettings settings = OneProfile();
    NavigationCookInput input;
    input.Positions = geometry.Positions;
    input.Indices = geometry.Indices;
    input.Settings = &settings;
    const NavigationCookResult cooked = CookZoneNavigation(input);
    ASSERT_TRUE(cooked.Navigation.has_value());
    const std::vector<std::byte> bytes = EncodeNavigationFile(*cooked.Navigation);

    NavigationFile file;
    std::vector<std::byte> corrupt = bytes;
    corrupt[corrupt.size() / 2] ^= std::byte{ 0x40 };
    EXPECT_FALSE(DecodeNavigationFile(corrupt, file));

    std::vector<std::byte> newer = bytes;
    newer[4] = std::byte{ 0x7f };
    std::string error;
    EXPECT_FALSE(DecodeNavigationFile(newer, file, &error));
    EXPECT_NE(error.find("version"), std::string::npos);

    EXPECT_TRUE(DecodeNavigationFile(bytes, file));
}

TEST(NavigationSettings, ParsesAndValidates)
{
    std::optional<JsonValue> good = JsonParse(R"({
        "type": "navigation.settings", "version": 1,
        "data": {
            "profiles": [ { "tag": "navigation.profile.humanoid", "radius": 0.3,
                            "height": 1.8, "max_slope_degrees": 45, "max_climb": 0.35,
                            "cell_size": 0.15, "cell_height": 0.1, "tile_cells": 64 } ],
            "areas": [ "navigation.area.water", "navigation.area.hazard" ] } })");
    ASSERT_TRUE(good.has_value());
    NavigationSettings settings;
    std::vector<CookDiagnostic> diagnostics;
    EXPECT_TRUE(ParseNavigationSettings(*good, settings, diagnostics));
    ASSERT_EQ(settings.Profiles.size(), 1u);
    EXPECT_EQ(settings.Profiles[0].Build.TileCells, 64u);
    EXPECT_EQ(settings.Areas.size(), 2u);

    std::optional<JsonValue> bad = JsonParse(R"({
        "type": "navigation.settings", "version": 1,
        "data": { "profiles": [ { "tag": "a", "radius": -1 }, { "tag": "b" }, { "tag": "b" } ],
                  "areas": [ "navigation.area.default" ] } })");
    ASSERT_TRUE(bad.has_value());
    diagnostics.clear();
    EXPECT_FALSE(ParseNavigationSettings(*bad, settings, diagnostics));
    EXPECT_EQ(diagnostics.size(), 3u);
}
