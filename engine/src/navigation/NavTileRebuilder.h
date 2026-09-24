#pragma once

#include "NavGeometryTracker.h"

#include <navigation/NavTileBuild.h>
#include <zone/ZoneId.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <tuple>

class JobSystem;
class RuntimeWorld;
class ZoneNavigation;

// The tiles runtime geometry has invalidated, and the budgeted work of
// rebuilding them. Dirty tiles are kept in (zone, profile, tile) order, so the
// order they rebuild and publish in never depends on the order changes were
// noticed. Builds run in parallel, one output slot each; publication is serial.
class NavTileRebuilder
{
public:
    struct BatchResult
    {
        std::size_t Rebuilt = 0;
        // Links whose attachment moved because a tile under an anchor rebuilt.
        std::size_t LinksReattached = 0;
    };

    // Marks every cooked tile of every profile of `zone` that `bounds` reaches.
    void MarkDirty(const ZoneNavigation& zone, const Aabb3d& bounds);
    void ForgetZone(ZoneId zone);
    [[nodiscard]] std::size_t Pending() const { return Dirty.size(); }

    // Rebuilds and publishes up to `budget` dirty tiles from their cooked
    // triangles plus `contributors`. `jobs` may be null: the serial path.
    BatchResult RebuildNext(RuntimeWorld& runtime, std::span<const NavGeometryContributor> contributors,
                            JobSystem* jobs, std::size_t budget);

private:
    struct DirtyTile
    {
        ZoneId Zone;
        std::uint16_t Profile = 0;
        NavTileCoord Tile;

        friend bool operator<(const DirtyTile& a, const DirtyTile& b)
        {
            return std::tie(a.Zone.Value, a.Profile, a.Tile) < std::tie(b.Zone.Value, b.Profile, b.Tile);
        }
    };

    std::set<DirtyTile> Dirty;
};
