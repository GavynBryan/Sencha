#pragma once

#include "document/ZoneGraph.h"

#include <math/Vec.h>
#include <math/geometry/2d/Rect2d.h>
#include <zone/ZoneId.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

class WorldDocument;

// Whether a zone is open in the editor: the one being edited, one open
// beside it, or one not loaded.
enum class WorldMapZoneState : std::uint8_t
{
    Closed,
    Open,
    Focus,
};

struct WorldMapZone
{
    ZoneId Id;
    std::string Name;
    Rect2d Plate;
    WorldMapZoneState State = WorldMapZoneState::Closed;
    // The world's validation has something to say about it.
    bool Problem = false;
    // The camera is in it.
    bool Here = false;
};

// A dock or link between two zones, from one plate's centre to the other's.
struct WorldMapLink
{
    Vec2d From;
    Vec2d To;
    bool AToB = true;
    bool BToA = true;
    bool Warned = false;
    bool Failed = false;
};

// The world from above, fitted into a rectangle in window pixels: one plate per
// zone that has bounds, at its footprint (+X to the right, +Z up), the ways
// between them, and the camera.
struct WorldMap
{
    std::vector<WorldMapZone> Zones;
    std::vector<WorldMapLink> Links;
    std::optional<Vec2d> Camera;
    // Zones without bounds, which have no place on the map.
    std::size_t Unplaced = 0;
};

struct WorldMapRequest
{
    Rect2d Area;
    float Margin = 0.0f;
    // The smallest a plate is drawn, so a small room stays something to click.
    float MinimumPlate = 0.0f;
    std::optional<Vec3d> Camera = {};
    std::span<const ZoneId> Problems = {};
};

[[nodiscard]] WorldMap LayOutWorldMap(const WorldDocument& world, std::span<const ZoneGraphEdge> edges,
                                      const WorldMapRequest& request);

// The zone whose plate is under `point`: the smallest, where plates overlap.
[[nodiscard]] std::optional<ZoneId> ZoneAt(const WorldMap& map, Vec2d point);
