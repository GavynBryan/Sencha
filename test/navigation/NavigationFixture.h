#pragma once

#include <navigation/NavTileBuild.h>
#include <navigation/NavTileMesh.h>

#include <cmath>
#include <cstdint>
#include <vector>

// World-space triangle soup built from axis-aligned boxes, wound
// counter-clockwise seen from outside like cooked brush faces.
struct NavTestGeometry
{
    std::vector<Vec3d> Positions;
    std::vector<std::uint32_t> Indices;

    void AddBox(const Vec3d& min, const Vec3d& max)
    {
        const Vec3d c[8] = {
            { min.X, min.Y, min.Z }, { max.X, min.Y, min.Z },
            { max.X, min.Y, max.Z }, { min.X, min.Y, max.Z },
            { min.X, max.Y, min.Z }, { max.X, max.Y, min.Z },
            { max.X, max.Y, max.Z }, { min.X, max.Y, max.Z },
        };
        const Vec3d center = (min + max) * 0.5f;
        const int quads[6][4] = {
            { 4, 5, 6, 7 }, { 0, 1, 2, 3 }, { 0, 1, 5, 4 },
            { 3, 2, 6, 7 }, { 0, 3, 7, 4 }, { 1, 2, 6, 5 },
        };
        for (const auto& q : quads)
        {
            AddOutwardTriangle(c[q[0]], c[q[1]], c[q[2]], center);
            AddOutwardTriangle(c[q[0]], c[q[2]], c[q[3]], center);
        }
    }

    // A slab whose top surface is walkable at height y.
    void AddFloor(float minX, float minZ, float maxX, float maxZ, float y = 0.0f)
    {
        AddBox(Vec3d(minX, y - 0.5f, minZ), Vec3d(maxX, y, maxZ));
    }

    float MinY() const
    {
        float y = Positions.empty() ? 0.0f : Positions.front().Y;
        for (const Vec3d& p : Positions)
            y = std::min(y, p.Y);
        return y;
    }

    float MaxY() const
    {
        float y = Positions.empty() ? 0.0f : Positions.front().Y;
        for (const Vec3d& p : Positions)
            y = std::max(y, p.Y);
        return y;
    }

private:
    void AddOutwardTriangle(const Vec3d& a, const Vec3d& b, const Vec3d& c,
                            const Vec3d& center)
    {
        const Vec3d normal = (b - a).Cross(c - a);
        const Vec3d outward = (a + b + c) * (1.0f / 3.0f) - center;
        const std::uint32_t base = static_cast<std::uint32_t>(Positions.size());
        Positions.push_back(a);
        if (normal.Dot(outward) >= 0.0f)
        {
            Positions.push_back(b);
            Positions.push_back(c);
        }
        else
        {
            Positions.push_back(c);
            Positions.push_back(b);
        }
        Indices.push_back(base);
        Indices.push_back(base + 1);
        Indices.push_back(base + 2);
    }
};

// A 20 x 10 m room split by a wall with a gap at its north end (z > 7).
inline NavTestGeometry WallRoom()
{
    NavTestGeometry g;
    g.AddFloor(0.0f, 0.0f, 20.0f, 10.0f);
    g.AddBox(Vec3d(9.5f, 0.0f, 0.0f), Vec3d(10.5f, 3.0f, 7.0f));
    return g;
}

// Two floors, x 0-8 and x 12-20, separated by a 4 m gap.
inline NavTestGeometry TwoIslands()
{
    NavTestGeometry g;
    g.AddFloor(0.0f, 0.0f, 8.0f, 10.0f);
    g.AddFloor(12.0f, 0.0f, 20.0f, 10.0f);
    return g;
}

// A 40 x 10 m floor spanning several tiles along X.
inline NavTestGeometry LongHall()
{
    NavTestGeometry g;
    g.AddFloor(0.0f, 0.0f, 40.0f, 10.0f);
    return g;
}

inline NavBuildProfile TestProfile()
{
    NavBuildProfile profile;
    profile.Radius = 0.3f;
    profile.Height = 1.8f;
    profile.MaxSlopeDegrees = 45.0f;
    profile.MaxClimb = 0.35f;
    profile.CellSize = 0.2f;
    profile.CellHeight = 0.1f;
    profile.TileCells = 40; // 8 m tiles
    return profile;
}

inline NavTileBuildInput TileInput(const NavTestGeometry& geometry,
                                   const NavBuildProfile& profile, NavTileCoord tile)
{
    NavTileBuildInput input;
    input.Profile = profile;
    input.Tile = tile;
    input.Positions = geometry.Positions;
    input.Indices = geometry.Indices;
    input.MinY = geometry.MinY() - 1.0f;
    input.MaxY = geometry.MaxY() + profile.Height + 1.0f;
    return input;
}

#include <assets/cook/NavigationCook.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <navigation/NavLinkComponent.h>
#include <navigation/NavigationFile.h>
#include <navigation/NavigationQuery.h>
#include <navigation/ZoneNavigation.h>

#include <gtest/gtest.h>

#include <string>

// A cooked-and-loaded zone for runtime query tests: geometry and links go
// through the real cook, the real file codec, and ZoneNavigation::Load.
struct NavZoneFixture
{
    GameplayTagRegistry Tags;
    GameplayTagId Humanoid;
    GameplayTagId Jump;
    GameplayTagId Drop;
    GameplayTagId Teleport;
    GameplayTagId Water;
    ZoneId Zone{ 0x5eed };

    NavZoneFixture()
    {
        Humanoid = *Tags.RegisterTag("navigation.profile.humanoid");
        Jump = *Tags.RegisterTag("navigation.traversal.jump");
        Drop = *Tags.RegisterTag("navigation.traversal.drop");
        Teleport = *Tags.RegisterTag("navigation.traversal.teleport");
        Water = *Tags.RegisterTag("navigation.area.water");
    }

    static NavLinkRecord Link(std::uint64_t id, std::string traversal, Vec3d entry,
                                   Vec3d exit, std::uint32_t directions = NavLinkDirectionForward,
                                   float baseCost = 1.0f)
    {
        NavLinkRecord link;
        link.Id = NavLinkId{ id };
        link.Traversal = std::move(traversal);
        link.Directions = directions;
        link.BaseCost = baseCost;
        link.Entry = entry;
        link.Exit = exit;
        return link;
    }

    NavigationFile Cook(const NavTestGeometry& geometry,
                        std::vector<NavLinkRecord> links = {},
                        std::vector<NavAreaVolume> areas = {}) const
    {
        NavigationSettings settings;
        settings.Profiles.push_back({ "navigation.profile.humanoid", TestProfile() });
        settings.Areas = { "navigation.area.water" };
        NavigationCookInput input;
        input.Positions = geometry.Positions;
        input.Indices = geometry.Indices;
        input.Settings = &settings;
        input.Links = links;
        input.AreaVolumes = areas;
        const NavigationCookResult cooked = CookZoneNavigation(input);
        for (const CookDiagnostic& d : cooked.Diagnostics)
            ADD_FAILURE() << d.Rule << ": " << d.Message;
        EXPECT_TRUE(cooked.Navigation.has_value());
        return cooked.Navigation.value_or(NavigationFile{});
    }

    ZoneNavigation Load(const NavTestGeometry& geometry,
                        std::vector<NavLinkRecord> links = {},
                        std::vector<NavAreaVolume> areas = {}) const
    {
        ZoneNavigation zone;
        EXPECT_TRUE(zone.Load(Zone, Cook(geometry, std::move(links), std::move(areas)), &Tags));
        return zone;
    }

    NavQueryRequest Request(std::span<const GameplayTagId> capabilities = {},
                            const NavQueryPolicy* policy = nullptr) const
    {
        NavQueryRequest request;
        request.Zone = Zone;
        request.Profile = Humanoid;
        request.Policy = policy;
        request.Capabilities = capabilities;
        return request;
    }

    static NavLocation Project(const ZoneNavigation& zone, NavQueryContext& context,
                               const NavQueryRequest& request, Vec3d point)
    {
        const NavProjectResult result =
            NavProjectPoint(zone, context, request, point, Vec3d(1.0f, 2.0f, 1.0f));
        EXPECT_EQ(result.Status, NavStatus::Success) << "projecting " << point.X << ","
                                                     << point.Y << "," << point.Z;
        return result.Location;
    }
};
