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

// NavTileMesh::AttachLinkAnchor with a caller-owned query already bound to the
// mesh, for resolving many anchors at once.
[[nodiscard]] std::optional<NavAnchorAttachment> AttachLinkAnchor(
    const NavTileMesh& mesh, dtNavMeshQuery& query, const Vec3d& anchor, float entryRadius);
