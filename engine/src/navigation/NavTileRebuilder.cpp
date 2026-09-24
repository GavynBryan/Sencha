#include "NavTileRebuilder.h"

#include "ZoneNavigationBackend.h"

#include <jobs/JobSystem.h>
#include <navigation/ZoneNavigation.h>
#include <world/RuntimeWorld.h>

#include <algorithm>
#include <utility>

namespace
{
    // One tile's build: inputs gathered on the owner thread, built on any worker.
    struct TileBuild
    {
        ZoneNavigation::Backend* Zone = nullptr;
        std::uint16_t Profile = 0;
        NavTileCoord Tile;
        std::vector<Vec3d> Positions;
        std::vector<std::uint32_t> Indices;
        float MinY = 0.0f;
        float MaxY = 0.0f;
        std::vector<std::byte> Output;
        NavTileBuildResult Result = NavTileBuildResult::Failed;

        void Run()
        {
            const NavBuildProfile& build = Zone->Profiles[Profile].Mesh.Profile();
            Result = BuildNavTile(NavTileBuildInput{ build, Tile, Positions, Indices, {}, MinY, MaxY },
                                  Output);
        }
    };

    // The tile's cooked triangles in their cooked order, then every
    // contributor overlapping it, so an unchanged tile rebuilds to its cooked bytes.
    void GatherInputs(TileBuild& job, std::span<const NavGeometryContributor> contributors)
    {
        const ZoneNavProfile& profile = job.Zone->Profiles[job.Profile];
        job.MinY = job.Zone->MinY;
        job.MaxY = job.Zone->MaxY;
        if (const NavTileRecord* source = FindSourceTile(profile, job.Tile))
            for (const std::uint32_t triangle : source->Triangles)
                for (int corner = 0; corner < 3; ++corner)
                {
                    job.Indices.push_back(static_cast<std::uint32_t>(job.Positions.size()));
                    job.Positions.push_back(
                        job.Zone->Positions[job.Zone->Indices[triangle * 3 + corner]]);
                }

        const NavBuildProfile& build = profile.Mesh.Profile();
        const Aabb3d reach = NavTileBuildBounds(build, job.Tile, -1e6f, 1e6f);
        for (const NavGeometryContributor& contributor : contributors)
        {
            if (!contributor.Bounds.Intersects(reach))
                continue;
            contributor.AppendTriangles(job.Positions, job.Indices);
            job.MinY = std::min(job.MinY, contributor.Bounds.Min.Y - 1.0f);
            job.MaxY = std::max(job.MaxY, contributor.Bounds.Max.Y + build.Height + 1.0f);
        }
    }
}

void NavTileRebuilder::MarkDirty(const ZoneNavigation& zone, const Aabb3d& bounds)
{
    const ZoneNavigation::Backend& backend = zone.GetBackend();
    for (std::uint16_t p = 0; p < backend.Profiles.size(); ++p)
    {
        const std::optional<NavTileRange>& cooked = backend.Profiles[p].CookedTiles;
        if (!cooked)
            continue;
        const NavTileRange touched = NavTilesTouching(backend.Profiles[p].Mesh.Profile(), bounds);
        for (std::int32_t z = std::max(touched.Min.Z, cooked->Min.Z);
             z <= std::min(touched.Max.Z, cooked->Max.Z); ++z)
            for (std::int32_t x = std::max(touched.Min.X, cooked->Min.X);
                 x <= std::min(touched.Max.X, cooked->Max.X); ++x)
                Dirty.insert(DirtyTile{ backend.Zone, p, NavTileCoord{ x, z } });
    }
}

void NavTileRebuilder::ForgetZone(ZoneId zone)
{
    std::erase_if(Dirty, [zone](const DirtyTile& tile) { return tile.Zone == zone; });
}

NavTileRebuilder::BatchResult NavTileRebuilder::RebuildNext(
    RuntimeWorld& runtime, std::span<const NavGeometryContributor> contributors, JobSystem* jobs,
    std::size_t budget)
{
    BatchResult result;
    std::vector<TileBuild> batch;
    while (!Dirty.empty() && batch.size() < budget)
    {
        const DirtyTile dirty = *Dirty.begin();
        Dirty.erase(Dirty.begin());
        ZoneNavigation* zone = FindZoneNavigation(runtime, dirty.Zone);
        if (zone == nullptr)
            continue;
        TileBuild& job = batch.emplace_back();
        job.Zone = &zone->GetBackend();
        job.Profile = dirty.Profile;
        job.Tile = dirty.Tile;
        GatherInputs(job, contributors);
    }

    const auto build = [&batch](std::uint32_t i) { batch[i].Run(); };
    if (jobs != nullptr)
        jobs->ParallelFor(static_cast<std::uint32_t>(batch.size()), build);
    else
        for (std::uint32_t i = 0; i < batch.size(); ++i)
            build(i);

    std::vector<std::pair<ZoneNavigation::Backend*, std::uint16_t>> touchedProfiles;
    for (TileBuild& job : batch)
    {
        if (job.Result == NavTileBuildResult::Failed)
            continue;
        (void)job.Zone->Profiles[job.Profile].Mesh.SetTile(job.Tile, job.Output);
        ++result.Rebuilt;
        const auto key = std::make_pair(job.Zone, job.Profile);
        if (std::ranges::find(touchedProfiles, key) == touchedProfiles.end())
            touchedProfiles.push_back(key);
    }
    std::vector<std::uint32_t> moved;
    for (const auto& [zone, profile] : touchedProfiles)
    {
        moved.clear();
        AttachNavLinks(*zone, zone->Profiles[profile], &moved);
        BumpNavLinkRevisions(*zone, moved);
        result.LinksReattached += moved.size();
    }
    return result;
}
