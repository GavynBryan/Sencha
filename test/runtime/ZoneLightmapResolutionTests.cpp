// Baked-lighting atlases are per zone, and several zones are resident at once
// in one World. Resolution therefore has to be keyed on the storage partition
// the mesh lives in: resolving one atlas for the whole World stamps whichever
// zone happened to be visited last onto every other zone's meshes.
//
// The pieces asserted here are the pure halves of that path. Turning a
// TextureHandle into a bindless index needs a live TextureCache and so a device;
// what decides which atlas a chunk samples does not, and that is what regressed.

#include <gtest/gtest.h>
#include <render/RenderComponentSchemas.h>

#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <render/extract/RenderExtractionSystem.h>
#include <render/ZoneLightmapComponent.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace
{
constexpr StoragePartitionId kZoneA{ 1 };
constexpr StoragePartitionId kZoneB{ 2 };
constexpr StoragePartitionId kZoneC{ 5 };

// TextureHandle identity is all these tests need; no GPU resource is created.
TextureHandle MakeTexture(std::uint32_t index)
{
    TextureHandle handle{};
    handle.Index = index;
    handle.Generation = 1;
    return handle;
}

EntityId AddZoneLightmap(
    World& world,
    StoragePartitionId partition,
    TextureHandle atlas,
    TextureHandle ao)
{
    const EntityId entity = world.CreateEntity(partition);
    world.AddComponent<ZoneLightmapComponent>(
        entity,
        ZoneLightmapComponent{ atlas, ao });
    return entity;
}

World MakeLightmapWorld()
{
    World world;
    world.RegisterComponent<ZoneLightmapComponent>();
    return world;
}
} // namespace

TEST(ZoneLightmapResolution, CollectsOneBindingPerLightmappedPartition)
{
    World world = MakeLightmapWorld();
    AddZoneLightmap(world, kZoneA, MakeTexture(10), MakeTexture(11));
    AddZoneLightmap(world, kZoneB, MakeTexture(20), MakeTexture(21));

    StoragePartitionSet partitions;
    partitions.Add(kZoneA);
    partitions.Add(kZoneB);

    std::vector<ZoneLightmapBinding> bindings;
    CollectZoneLightmaps(world, partitions, bindings);

    ASSERT_EQ(bindings.size(), 2u);
    // Each zone keeps its own atlas; neither one wins globally.
    for (const ZoneLightmapBinding& binding : bindings)
    {
        if (binding.Partition == kZoneA)
        {
            EXPECT_EQ(binding.Texture.Index, 10u);
            EXPECT_EQ(binding.Ao.Index, 11u);
        }
        else
        {
            EXPECT_EQ(binding.Partition, kZoneB);
            EXPECT_EQ(binding.Texture.Index, 20u);
            EXPECT_EQ(binding.Ao.Index, 21u);
        }
    }
}

TEST(ZoneLightmapResolution, SkipsPartitionsOutsideTheVisibleSet)
{
    World world = MakeLightmapWorld();
    AddZoneLightmap(world, kZoneA, MakeTexture(10), MakeTexture(11));
    AddZoneLightmap(world, kZoneB, MakeTexture(20), MakeTexture(21));

    StoragePartitionSet visible;
    visible.Add(kZoneA);

    std::vector<ZoneLightmapBinding> bindings;
    CollectZoneLightmaps(world, visible, bindings);

    ASSERT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings[0].Partition, kZoneA);
    EXPECT_EQ(bindings[0].Texture.Index, 10u);
}

TEST(ZoneLightmapResolution, WorldWithoutLightmapsCollectsNothing)
{
    World world;
    StoragePartitionSet partitions;
    partitions.Add(kZoneA);

    std::vector<ZoneLightmapBinding> bindings;
    bindings.push_back(ZoneLightmapBinding{});
    CollectZoneLightmaps(world, partitions, bindings);

    // Unregistered component: the out parameter is still cleared, so a stale
    // binding from a previous frame cannot leak into this one.
    EXPECT_TRUE(bindings.empty());
}

// The regression itself: two lightmapped zones resident, and each partition's
// chunks must read back that partition's own indices. Before per-partition
// resolution the extractor kept a single pair of indices for the whole World,
// so both zones reported the same atlas.
TEST(ZoneLightmapResolution, EachPartitionLooksUpItsOwnAtlas)
{
    const std::vector<std::pair<StoragePartitionId, ZoneLightmapIndices>> resolved{
        { kZoneA, ZoneLightmapIndices{ 10u, 11u } },
        { kZoneB, ZoneLightmapIndices{ 20u, 21u } },
    };

    std::vector<ZoneLightmapIndices> table;
    BuildZoneLightmapTable(resolved, table);

    const ZoneLightmapIndices fromA = LookupZoneLightmap(table, kZoneA);
    EXPECT_EQ(fromA.Lightmap, 10u);
    EXPECT_EQ(fromA.Ao, 11u);

    const ZoneLightmapIndices fromB = LookupZoneLightmap(table, kZoneB);
    EXPECT_EQ(fromB.Lightmap, 20u);
    EXPECT_EQ(fromB.Ao, 21u);
}

TEST(ZoneLightmapResolution, PartitionsWithoutALightmapReadBackInvalid)
{
    const std::vector<std::pair<StoragePartitionId, ZoneLightmapIndices>> resolved{
        { kZoneC, ZoneLightmapIndices{ 50u, 51u } },
    };

    std::vector<ZoneLightmapIndices> table;
    BuildZoneLightmapTable(resolved, table);

    // A gap below the highest populated slot, and everything past the end:
    // both mean "this zone baked no lighting", never another zone's atlas.
    const ZoneLightmapIndices gap = LookupZoneLightmap(table, kZoneA);
    EXPECT_EQ(gap.Lightmap, UINT32_MAX);
    EXPECT_EQ(gap.Ao, UINT32_MAX);

    const ZoneLightmapIndices past =
        LookupZoneLightmap(table, StoragePartitionId{ 9 });
    EXPECT_EQ(past.Lightmap, UINT32_MAX);
    EXPECT_EQ(past.Ao, UINT32_MAX);

    // The persistent partition has no lightmap of its own either.
    const ZoneLightmapIndices persistent =
        LookupZoneLightmap(table, StoragePartitionId::Default());
    EXPECT_EQ(persistent.Lightmap, UINT32_MAX);
}

TEST(ZoneLightmapResolution, EmptyResolutionYieldsAnEmptyTable)
{
    std::vector<ZoneLightmapIndices> table;
    table.push_back(ZoneLightmapIndices{ 7u, 8u });
    BuildZoneLightmapTable({}, table);

    EXPECT_TRUE(table.empty());
    EXPECT_EQ(LookupZoneLightmap(table, kZoneA).Lightmap, UINT32_MAX);
}
