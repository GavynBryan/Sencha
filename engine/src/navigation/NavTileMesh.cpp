#include <navigation/NavTileMesh.h>

#include "NavTileMeshBackend.h"

#include <DetourAlloc.h>
#include <DetourNavMeshQuery.h>

#include <algorithm>
#include <cmath>
#include <cstring>

NavTileMesh::Backend::~Backend()
{
    if (Mesh != nullptr)
        dtFreeNavMesh(Mesh);
}

NavTileMesh::NavTileMesh() : Impl(std::make_unique<Backend>()) {}
NavTileMesh::~NavTileMesh() = default;
NavTileMesh::NavTileMesh(NavTileMesh&&) noexcept = default;
NavTileMesh& NavTileMesh::operator=(NavTileMesh&&) noexcept = default;

bool NavTileMesh::Init(const NavBuildProfile& profile, std::uint32_t maxTiles)
{
    if (!IsValidNavBuildProfile(profile) || maxTiles == 0)
        return false;
    if (Impl->Mesh != nullptr)
    {
        dtFreeNavMesh(Impl->Mesh);
        Impl->Mesh = nullptr;
    }
    Impl->Profile = profile;
    Impl->Tiles = 0;
    Impl->Polygons = 0;

    dtNavMesh* mesh = dtAllocNavMesh();
    if (mesh == nullptr)
        return false;
    // The tile grid is anchored at the world origin, matching NavTileAt, so a
    // tile's coordinates mean the same region to the cook and the runtime.
    dtNavMeshParams params{};
    params.orig[0] = 0.0f;
    params.orig[1] = 0.0f;
    params.orig[2] = 0.0f;
    params.tileWidth = profile.TileWorldSize();
    params.tileHeight = profile.TileWorldSize();
    params.maxTiles = static_cast<int>(maxTiles);
    params.maxPolys = 1 << 16;
    if (dtStatusFailed(mesh->init(&params)))
    {
        dtFreeNavMesh(mesh);
        return false;
    }
    Impl->Mesh = mesh;
    return true;
}

bool NavTileMesh::SetTile(NavTileCoord tile, std::span<const std::byte> data)
{
    dtNavMesh* mesh = Impl->Mesh;
    if (mesh == nullptr)
        return false;

    if (const dtTileRef existing = mesh->getTileRefAt(tile.X, tile.Z, 0); existing != 0)
    {
        const dtMeshTile* old = mesh->getTileAt(tile.X, tile.Z, 0);
        const std::size_t oldPolys =
            old != nullptr && old->header != nullptr
                ? static_cast<std::size_t>(old->header->polyCount) : 0;
        if (dtStatusFailed(mesh->removeTile(existing, nullptr, nullptr)))
            return false;
        --Impl->Tiles;
        Impl->Polygons -= oldPolys;
    }
    if (data.empty())
        return true;

    // Detour takes ownership of a buffer from its own allocator and frees it
    // when the tile is removed or the mesh destroyed.
    auto* owned = static_cast<unsigned char*>(dtAlloc(static_cast<int>(data.size()),
                                                      DT_ALLOC_PERM));
    if (owned == nullptr)
        return false;
    std::memcpy(owned, data.data(), data.size());
    const auto* header = reinterpret_cast<const dtMeshHeader*>(owned);
    if (data.size() < sizeof(dtMeshHeader) || header->magic != DT_NAVMESH_MAGIC
        || header->version != DT_NAVMESH_VERSION || header->x != tile.X
        || header->y != tile.Z || header->layer != 0)
    {
        dtFree(owned);
        return false;
    }
    const int polyCount = header->polyCount;
    if (dtStatusFailed(mesh->addTile(owned, static_cast<int>(data.size()),
                                     DT_TILE_FREE_DATA, 0, nullptr)))
    {
        dtFree(owned);
        return false;
    }
    ++Impl->Tiles;
    Impl->Polygons += static_cast<std::size_t>(polyCount);
    return true;
}

std::uint64_t NavTileMesh::TileRevision(NavTileCoord tile) const
{
    if (Impl->Mesh == nullptr)
        return 0;
    return static_cast<std::uint64_t>(Impl->Mesh->getTileRefAt(tile.X, tile.Z, 0));
}

const NavBuildProfile& NavTileMesh::Profile() const { return Impl->Profile; }
std::size_t NavTileMesh::TileCount() const { return Impl->Tiles; }
std::size_t NavTileMesh::PolygonCount() const { return Impl->Polygons; }

std::optional<Vec3d> NavTileMesh::ProjectForTooling(const Vec3d& point,
                                                    const Vec3d& halfExtents) const
{
    if (Impl->Mesh == nullptr || Impl->Tiles == 0)
        return std::nullopt;
    dtNavMeshQuery* query = dtAllocNavMeshQuery();
    if (query == nullptr)
        return std::nullopt;
    std::optional<Vec3d> result;
    if (dtStatusSucceed(query->init(Impl->Mesh, 64)))
    {
        const float center[3] = { point.X, point.Y, point.Z };
        const float extents[3] = { halfExtents.X, halfExtents.Y, halfExtents.Z };
        dtQueryFilter filter;
        dtPolyRef ref = 0;
        float nearest[3] = {};
        if (dtStatusSucceed(query->findNearestPoly(center, extents, &filter, &ref, nearest))
            && ref != 0)
            result = Vec3d(nearest[0], nearest[1], nearest[2]);
    }
    dtFreeNavMeshQuery(query);
    return result;
}

std::optional<NavAnchorAttachment> AttachLinkAnchor(const NavTileMesh& mesh,
                                                    dtNavMeshQuery& query,
                                                    const Vec3d& anchor, float entryRadius)
{
    const NavBuildProfile& profile = mesh.Profile();
    const float reach = std::max(entryRadius, profile.Radius);
    const float center[3] = { anchor.X, anchor.Y, anchor.Z };
    const float extents[3] = { reach, profile.MaxClimb + profile.Height, reach };
    dtQueryFilter filter;
    dtPolyRef polygon = 0;
    float nearest[3] = {};
    if (dtStatusFailed(query.findNearestPoly(center, extents, &filter, &polygon, nearest))
        || polygon == 0)
        return std::nullopt;
    // findNearestPoly returns the nearest point on any polygon whose bounds
    // reach the box, which can lie outside it; hold it to the reach.
    const float dx = nearest[0] - anchor.X;
    const float dz = nearest[2] - anchor.Z;
    if (std::sqrt(dx * dx + dz * dz) > reach)
        return std::nullopt;
    return NavAnchorAttachment{ polygon, Vec3d(nearest[0], nearest[1], nearest[2]) };
}

std::optional<NavAnchorAttachment> NavTileMesh::AttachLinkAnchor(const Vec3d& anchor,
                                                                 float entryRadius) const
{
    if (Impl->Mesh == nullptr || Impl->Tiles == 0)
        return std::nullopt;
    dtNavMeshQuery* query = dtAllocNavMeshQuery();
    std::optional<NavAnchorAttachment> result;
    if (query != nullptr && dtStatusSucceed(query->init(Impl->Mesh, 64)))
        result = ::AttachLinkAnchor(*this, *query, anchor, entryRadius);
    if (query != nullptr)
        dtFreeNavMeshQuery(query);
    return result;
}
