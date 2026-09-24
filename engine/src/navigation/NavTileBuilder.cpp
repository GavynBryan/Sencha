#include <navigation/NavTileBuild.h>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace
{
    constexpr int kMaxVertsPerPoly = DT_VERTS_PER_POLYGON;
    constexpr std::uint32_t kMaxTileVertices = 0xffffu;
    // Every polygon is traversable; area exclusion is applied at query time.
    constexpr unsigned short kWalkablePolyFlag = 1u;

    struct HeightfieldDeleter { void operator()(rcHeightfield* p) const { rcFreeHeightField(p); } };
    struct CompactDeleter { void operator()(rcCompactHeightfield* p) const { rcFreeCompactHeightfield(p); } };
    struct ContourDeleter { void operator()(rcContourSet* p) const { rcFreeContourSet(p); } };
    struct PolyMeshDeleter { void operator()(rcPolyMesh* p) const { rcFreePolyMesh(p); } };
    struct DetailDeleter { void operator()(rcPolyMeshDetail* p) const { rcFreePolyMeshDetail(p); } };

    using CompactHeightfield = std::unique_ptr<rcCompactHeightfield, CompactDeleter>;
    using PolyMesh = std::unique_ptr<rcPolyMesh, PolyMeshDeleter>;
    using PolyMeshDetail = std::unique_ptr<rcPolyMeshDetail, DetailDeleter>;

    int BorderCells(const NavBuildProfile& profile)
    {
        return static_cast<int>(std::ceil(profile.Radius / profile.CellSize)) + 3;
    }

    bool Finite(float value) { return std::isfinite(value); }

    // The profile's agent dimensions in voxels.
    struct AgentVoxels
    {
        int Height = 0;
        int Climb = 0;
        int Radius = 0;

        explicit AgentVoxels(const NavBuildProfile& profile)
            : Height(static_cast<int>(std::ceil(profile.Height / profile.CellHeight)))
            , Climb(static_cast<int>(std::floor(profile.MaxClimb / profile.CellHeight)))
            , Radius(static_cast<int>(std::ceil(profile.Radius / profile.CellSize)))
        {
        }
    };

    // Triangles reaching the expanded tile, copied into the flat arrays Recast
    // rasterizes. Input order is kept, so the bytes depend only on the input.
    void GatherTileTriangles(const NavTileBuildInput& input, const Aabb3d& bounds,
                             std::vector<float>& verts, std::vector<int>& tris)
    {
        const std::size_t triangleCount = input.Indices.size() / 3;
        for (std::size_t t = 0; t < triangleCount; ++t)
        {
            const std::uint32_t* corner = &input.Indices[t * 3];
            if (corner[0] >= input.Positions.size() || corner[1] >= input.Positions.size()
                || corner[2] >= input.Positions.size())
                continue;
            const Vec3d& a = input.Positions[corner[0]];
            const Vec3d& b = input.Positions[corner[1]];
            const Vec3d& c = input.Positions[corner[2]];
            if (!NavTriangleTouchesTile(bounds, a, b, c))
                continue;
            const int base = static_cast<int>(verts.size() / 3);
            for (const Vec3d* vertex : { &a, &b, &c })
                verts.insert(verts.end(), { vertex->X, vertex->Y, vertex->Z });
            tris.insert(tris.end(), { base, base + 1, base + 2 });
        }
    }

    // Rasterizes the triangles, drops spans an agent cannot stand on, and
    // erodes the walkable area by the agent radius.
    CompactHeightfield BuildWalkableField(rcContext& context, const NavBuildProfile& profile,
                                          const AgentVoxels& agent, const Aabb3d& bounds,
                                          std::vector<float>& verts, std::vector<int>& tris)
    {
        const int gridSize = static_cast<int>(profile.TileCells) + BorderCells(profile) * 2;
        const float bmin[3] = { bounds.Min.X, bounds.Min.Y, bounds.Min.Z };
        const float bmax[3] = { bounds.Max.X, bounds.Max.Y, bounds.Max.Z };
        std::unique_ptr<rcHeightfield, HeightfieldDeleter> solid(rcAllocHeightfield());
        if (!solid || !rcCreateHeightfield(&context, *solid, gridSize, gridSize, bmin, bmax,
                                           profile.CellSize, profile.CellHeight))
            return nullptr;

        const int vertCount = static_cast<int>(verts.size() / 3);
        const int triCount = static_cast<int>(tris.size() / 3);
        std::vector<unsigned char> triAreas(static_cast<std::size_t>(triCount), RC_NULL_AREA);
        rcMarkWalkableTriangles(&context, profile.MaxSlopeDegrees, verts.data(), vertCount,
                                tris.data(), triCount, triAreas.data());
        if (!rcRasterizeTriangles(&context, verts.data(), vertCount, tris.data(),
                                  triAreas.data(), triCount, *solid, agent.Climb))
            return nullptr;
        rcFilterLowHangingWalkableObstacles(&context, agent.Climb, *solid);
        rcFilterLedgeSpans(&context, agent.Height, agent.Climb, *solid);
        rcFilterWalkableLowHeightSpans(&context, agent.Height, *solid);

        CompactHeightfield compact(rcAllocCompactHeightfield());
        if (!compact
            || !rcBuildCompactHeightfield(&context, agent.Height, agent.Climb, *solid, *compact)
            || !rcErodeWalkableArea(&context, agent.Radius, *compact))
            return nullptr;
        return compact;
    }

    // Area volumes in input order: a later volume wins where two overlap.
    void MarkAreaVolumes(rcContext& context, std::span<const NavAreaVolume> areas,
                         rcCompactHeightfield& compact)
    {
        std::vector<float> footprint;
        for (const NavAreaVolume& volume : areas)
        {
            if (volume.Footprint.size() < 3 || volume.Area == kNavDefaultArea
                || volume.Area > kNavMaxAuthoredAreas)
                continue;
            footprint.clear();
            for (const Vec3d& point : volume.Footprint)
                footprint.insert(footprint.end(), { point.X, point.Y, point.Z });
            rcMarkConvexPolyArea(&context, footprint.data(),
                                 static_cast<int>(volume.Footprint.size()),
                                 volume.MinY, volume.MaxY, volume.Area, compact);
        }
    }

    // Partitions the walkable field into convex polygons plus height detail.
    // Monotone partitioning needs no distance field and is deterministic.
    NavTileBuildResult BuildPolygons(rcContext& context, const NavBuildProfile& profile,
                                     rcCompactHeightfield& compact, PolyMesh& mesh,
                                     PolyMeshDetail& detail)
    {
        constexpr int kMinRegionArea = 8 * 8;
        constexpr int kMergeRegionArea = 20 * 20;
        constexpr float kMaxSimplificationError = 1.3f;
        if (!rcBuildRegionsMonotone(&context, compact, BorderCells(profile), kMinRegionArea,
                                    kMergeRegionArea))
            return NavTileBuildResult::Failed;

        std::unique_ptr<rcContourSet, ContourDeleter> contours(rcAllocContourSet());
        const int maxEdgeLength = static_cast<int>(12.0f / profile.CellSize);
        if (!contours || !rcBuildContours(&context, compact, kMaxSimplificationError,
                                          maxEdgeLength, *contours))
            return NavTileBuildResult::Failed;
        if (contours->nconts == 0)
            return NavTileBuildResult::Empty;

        mesh.reset(rcAllocPolyMesh());
        if (!mesh || !rcBuildPolyMesh(&context, *contours, kMaxVertsPerPoly, *mesh))
            return NavTileBuildResult::Failed;
        if (mesh->npolys == 0)
            return NavTileBuildResult::Empty;
        if (static_cast<std::uint32_t>(mesh->nverts) > kMaxTileVertices)
            return NavTileBuildResult::Failed;

        detail.reset(rcAllocPolyMeshDetail());
        if (!detail || !rcBuildPolyMeshDetail(&context, *mesh, compact, profile.CellSize * 6.0f,
                                              profile.CellHeight, *detail))
            return NavTileBuildResult::Failed;
        return NavTileBuildResult::Built;
    }

    // Serializes the polygons as a Detour tile at the input's coordinates.
    bool EncodeTile(const NavTileBuildInput& input, rcPolyMesh& mesh,
                    const rcPolyMeshDetail& detail, std::vector<std::byte>& tileData)
    {
        for (int i = 0; i < mesh.npolys; ++i)
        {
            // Recast's build-time walkable marker becomes the default area.
            if (mesh.areas[i] == RC_WALKABLE_AREA)
                mesh.areas[i] = kNavDefaultArea;
            mesh.flags[i] = kWalkablePolyFlag;
        }

        const NavBuildProfile& profile = input.Profile;
        dtNavMeshCreateParams params{};
        params.verts = mesh.verts;
        params.vertCount = mesh.nverts;
        params.polys = mesh.polys;
        params.polyAreas = mesh.areas;
        params.polyFlags = mesh.flags;
        params.polyCount = mesh.npolys;
        params.nvp = mesh.nvp;
        params.detailMeshes = detail.meshes;
        params.detailVerts = detail.verts;
        params.detailVertsCount = detail.nverts;
        params.detailTris = detail.tris;
        params.detailTriCount = detail.ntris;
        params.tileX = input.Tile.X;
        params.tileY = input.Tile.Z;
        std::memcpy(params.bmin, mesh.bmin, sizeof(params.bmin));
        std::memcpy(params.bmax, mesh.bmax, sizeof(params.bmax));
        params.walkableHeight = profile.Height;
        params.walkableRadius = profile.Radius;
        params.walkableClimb = profile.MaxClimb;
        params.cs = profile.CellSize;
        params.ch = profile.CellHeight;
        params.buildBvTree = true;

        unsigned char* data = nullptr;
        int dataSize = 0;
        if (!dtCreateNavMeshData(&params, &data, &dataSize) || data == nullptr)
            return false;
        tileData.resize(static_cast<std::size_t>(dataSize));
        std::memcpy(tileData.data(), data, static_cast<std::size_t>(dataSize));
        dtFree(data);
        return true;
    }
}

bool IsValidNavBuildProfile(const NavBuildProfile& profile)
{
    return Finite(profile.Radius) && profile.Radius >= 0.0f
        && Finite(profile.Height) && profile.Height > 0.0f
        && Finite(profile.MaxSlopeDegrees) && profile.MaxSlopeDegrees > 0.0f
        && profile.MaxSlopeDegrees < 90.0f
        && Finite(profile.MaxClimb) && profile.MaxClimb >= 0.0f
        && Finite(profile.CellSize) && profile.CellSize > 0.0f
        && Finite(profile.CellHeight) && profile.CellHeight > 0.0f
        && profile.TileCells >= kNavMinTileCells && profile.TileCells <= kNavMaxTileCells;
}

NavTileCoord NavTileAt(const NavBuildProfile& profile, const Vec3d& point)
{
    const float size = profile.TileWorldSize();
    return NavTileCoord{
        static_cast<std::int32_t>(std::floor(point.X / size)),
        static_cast<std::int32_t>(std::floor(point.Z / size)),
    };
}

Aabb3d NavTileBuildBounds(const NavBuildProfile& profile, NavTileCoord tile,
                          float minY, float maxY)
{
    const float size = profile.TileWorldSize();
    const float border = static_cast<float>(BorderCells(profile)) * profile.CellSize;
    return Aabb3d::FromMinMax(
        Vec3d(static_cast<float>(tile.X) * size - border, minY,
              static_cast<float>(tile.Z) * size - border),
        Vec3d(static_cast<float>(tile.X + 1) * size + border, maxY,
              static_cast<float>(tile.Z + 1) * size + border));
}

NavTileRange NavTilesTouching(const NavBuildProfile& profile, const Aabb3d& bounds)
{
    const float border = static_cast<float>(BorderCells(profile)) * profile.CellSize;
    const Vec3d reach(border, 0.0f, border);
    return NavTileRange{ NavTileAt(profile, bounds.Min - reach),
                         NavTileAt(profile, bounds.Max + reach) };
}

bool NavTriangleTouchesTile(const Aabb3d& tileBuildBounds, const Vec3d& a, const Vec3d& b,
                            const Vec3d& c)
{
    return std::max({ a.X, b.X, c.X }) >= tileBuildBounds.Min.X
        && std::min({ a.X, b.X, c.X }) <= tileBuildBounds.Max.X
        && std::max({ a.Z, b.Z, c.Z }) >= tileBuildBounds.Min.Z
        && std::min({ a.Z, b.Z, c.Z }) <= tileBuildBounds.Max.Z;
}

NavTileBuildResult BuildNavTile(const NavTileBuildInput& input,
                                std::vector<std::byte>& tileData)
{
    tileData.clear();
    const NavBuildProfile& profile = input.Profile;
    if (!IsValidNavBuildProfile(profile) || !Finite(input.MinY) || !Finite(input.MaxY)
        || input.MaxY <= input.MinY || input.Indices.size() % 3 != 0)
        return NavTileBuildResult::Failed;

    const Aabb3d bounds = NavTileBuildBounds(profile, input.Tile, input.MinY, input.MaxY);
    std::vector<float> verts;
    std::vector<int> tris;
    GatherTileTriangles(input, bounds, verts, tris);
    if (tris.empty())
        return NavTileBuildResult::Empty;

    // Timers and logging off: the context carries no state that affects output.
    rcContext context(false);
    CompactHeightfield walkable =
        BuildWalkableField(context, profile, AgentVoxels(profile), bounds, verts, tris);
    if (!walkable)
        return NavTileBuildResult::Failed;
    MarkAreaVolumes(context, input.Areas, *walkable);

    PolyMesh mesh;
    PolyMeshDetail detail;
    const NavTileBuildResult polygons = BuildPolygons(context, profile, *walkable, mesh, detail);
    if (polygons != NavTileBuildResult::Built)
        return polygons;
    return EncodeTile(input, *mesh, *detail, tileData) ? NavTileBuildResult::Built
                                                       : NavTileBuildResult::Failed;
}
