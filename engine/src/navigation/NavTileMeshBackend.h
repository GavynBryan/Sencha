#pragma once

#include <navigation/NavTileMesh.h>

#include <DetourNavMesh.h>

class dtNavMeshQuery;

// Private to the navigation module: the Detour mesh behind a NavTileMesh.
struct NavTileMesh::Backend
{
    NavBuildProfile Profile;
    dtNavMesh* Mesh = nullptr;
    std::size_t Tiles = 0;
    std::size_t Polygons = 0;

    Backend() = default;
    ~Backend();
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
};

// Detour takes positions as float[3].
inline void CopyToDetour(const Vec3d& v, float* out)
{
    out[0] = v.X;
    out[1] = v.Y;
    out[2] = v.Z;
}

// The stretch of `poly`'s edge that `link` crosses, as fractions along it:
// the whole edge inside a tile, the part the neighbour shares at a tile border.
struct NavEdgeSpan
{
    const float* From = nullptr;
    const float* To = nullptr;
    float Start = 0.0f;
    float End = 1.0f;
};

inline NavEdgeSpan SharedEdgeSpan(const dtMeshTile* tile, const dtPoly* poly, const dtLink& link)
{
    NavEdgeSpan span{ &tile->verts[poly->verts[link.edge] * 3],
                      &tile->verts[poly->verts[(link.edge + 1) % poly->vertCount] * 3] };
    if (link.side != 0xff && (link.bmin != 0 || link.bmax != 255))
    {
        span.Start = static_cast<float>(link.bmin) / 255.0f;
        span.End = static_cast<float>(link.bmax) / 255.0f;
    }
    return span;
}

// NavTileMesh::AttachLinkAnchor with a caller-owned query already bound to the
// mesh, for resolving many anchors at once.
[[nodiscard]] std::optional<NavAnchorAttachment> AttachLinkAnchor(
    const NavTileMesh& mesh, dtNavMeshQuery& query, const Vec3d& anchor, float entryRadius);
