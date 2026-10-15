#include <navigation/tooling/NavMeshSnapshot.h>

#include "../NavTileMeshBackend.h"
#include "../ZoneNavigationBackend.h"

#include <navigation/ZoneNavigation.h>

#include <DetourCommon.h>

#include <algorithm>
#include <unordered_map>

namespace
{
    Vec3d FromDetour(const float* v)
    {
        return Vec3d{ v[0], v[1], v[2] };
    }

    // Resident tiles by coordinate, so the snapshot does not depend on which
    // slot each tile happened to load into.
    std::vector<const dtMeshTile*> TilesInOrder(const dtNavMesh& mesh)
    {
        std::vector<const dtMeshTile*> tiles;
        for (int i = 0; i < mesh.getMaxTiles(); ++i)
            if (const dtMeshTile* tile = mesh.getTile(i); tile->header != nullptr)
                tiles.push_back(tile);
        std::ranges::sort(tiles, [](const dtMeshTile* a, const dtMeshTile* b) {
            if (a->header->x != b->header->x)
                return a->header->x < b->header->x;
            if (a->header->y != b->header->y)
                return a->header->y < b->header->y;
            return a->header->layer < b->header->layer;
        });
        return tiles;
    }
}

NavMeshSnapshot SnapshotNavMesh(const ZoneNavigation& zone, std::uint16_t profile)
{
    const ZoneNavigation::Backend& backend = zone.GetBackend();
    NavMeshSnapshot snapshot;
    snapshot.Zone = backend.Zone;
    snapshot.Generation = backend.Generation;
    snapshot.Profile = profile;
    if (profile >= backend.Profiles.size())
        return snapshot;
    const dtNavMesh* mesh = backend.Profiles[profile].Mesh.GetBackend().Mesh;
    if (mesh == nullptr)
        return snapshot;

    struct Walkable
    {
        const dtMeshTile* Tile;
        const dtPoly* Poly;
    };
    std::vector<Walkable> walkable;
    std::unordered_map<dtPolyRef, std::uint32_t> indexOf;
    for (const dtMeshTile* tile : TilesInOrder(*mesh))
    {
        const dtPolyRef base = mesh->getPolyRefBase(tile);
        for (int p = 0; p < tile->header->polyCount; ++p)
        {
            const dtPoly& poly = tile->polys[p];
            if (poly.getType() != DT_POLYTYPE_GROUND)
                continue;
            const dtPolyRef ref = base | static_cast<dtPolyRef>(p);
            indexOf.emplace(ref, static_cast<std::uint32_t>(walkable.size()));
            walkable.push_back({ tile, &poly });

            NavSnapshotPolygon polygon;
            polygon.Region = NavRegion{ backend.Zone, backend.Generation, profile, 0, ref };
            polygon.Area = AreaTagOf(backend, poly.getArea());
            polygon.FirstVertex = static_cast<std::uint32_t>(snapshot.Vertices.size());
            polygon.VertexCount = poly.vertCount;
            for (int v = 0; v < poly.vertCount; ++v)
                snapshot.Vertices.push_back(FromDetour(&tile->verts[poly.verts[v] * 3]));
            snapshot.Polygons.push_back(polygon);
        }
    }

    for (std::uint32_t index = 0; index < walkable.size(); ++index)
    {
        const auto [tile, poly] = walkable[index];
        std::vector<bool> met(poly->vertCount, false);
        for (unsigned int l = poly->firstLink; l != DT_NULL_LINK; l = tile->links[l].next)
        {
            const dtLink& link = tile->links[l];
            const auto neighbour = indexOf.find(link.ref);
            if (link.ref == 0 || neighbour == indexOf.end())
                continue;
            met[link.edge] = true;
            if (neighbour->second < index)
                continue;
            const NavEdgeSpan span = SharedEdgeSpan(tile, poly, link);
            float a[3];
            float b[3];
            dtVlerp(a, span.From, span.To, span.Start);
            dtVlerp(b, span.From, span.To, span.End);
            snapshot.Portals.push_back({ index, neighbour->second, FromDetour(a), FromDetour(b) });
        }
        for (int edge = 0; edge < poly->vertCount; ++edge)
            if (!met[edge])
                snapshot.Borders.push_back({ index, FromDetour(&tile->verts[poly->verts[edge] * 3]),
                                             FromDetour(&tile->verts[poly->verts[(edge + 1) % poly->vertCount] * 3]) });
    }
    return snapshot;
}
