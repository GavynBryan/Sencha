// Residency rules of the viewport's brush bake cache: one baked mesh per
// distinct mesh content, nothing baked for a placement-only change, nothing
// evicted for a hidden brush.

#include "render/BrushBakeCache.h"

#include "brush/BrushOps.h"
#include "brush/MirrorModifier.h"

#include <assets/static_mesh/MeshGeometry.h>

#include <gtest/gtest.h>

namespace
{
class BrushBakeCacheTest : public ::testing::Test
{
protected:
    BrushBakeCacheTest()
        : Cache(BrushBakeCache::Gpu{
              .Bake = [this](const MeshGeometry& geometry)
              {
                  LastVertexCount = geometry.Vertices.size();
                  return StaticMeshHandle{ NextMesh++, 1 };
              },
              .Destroy = [this](StaticMeshHandle handle) { Destroyed.push_back(handle); },
              .Lease = [this](const AssetRef&) { ++Leases; return MaterialHandle{ NextMaterial++, 1 }; },
              .Release = [this](MaterialHandle) { ++Releases; },
          })
    {
    }

    static BrushModifier Array(int count)
    {
        BrushModifier m;
        ArrayModifier array;
        array.Count = count;
        m.Params = array;
        return m;
    }

    static BrushModifier Mirror(float offset)
    {
        BrushModifier m;
        MirrorModifier mirror;
        mirror.Offset = offset;
        m.Params = mirror;
        return m;
    }

    [[nodiscard]] BrushEvaluated Evaluate(const BrushMesh& mesh, const BrushModifierStack& stack) const
    {
        return EvaluateBrushModifiers(mesh, stack, BrushEvaluationPolicy::Cook());
    }

    const RegistryId Registry{ 1, 1 };
    const BrushId Brush{ 7 };
    const AssetRef Default{ AssetType::Material, "asset://materials/dev/gray.smat" };
    std::uint32_t NextMesh = 1;
    std::uint32_t NextMaterial = 1;
    std::size_t Leases = 0;
    std::size_t Releases = 0;
    std::size_t LastVertexCount = 0;
    std::vector<StaticMeshHandle> Destroyed;
    // Last: its destructor releases every mesh through the callbacks above,
    // which must still be alive.
    BrushBakeCache Cache;
};
}

TEST_F(BrushBakeCacheTest, PlainBrushBakesOnceAndLeasesItsMaterial)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushBakedRecord& record = Cache.Ensure(Registry, Brush, Evaluate(box, {}), Default);
    ASSERT_EQ(record.Meshes.size(), 1u);
    EXPECT_TRUE(record.Meshes[0].Handle.IsValid());
    EXPECT_EQ(record.Meshes[0].Signature, BrushMeshSignature(box));
    EXPECT_EQ(record.Meshes[0].SlotMaterials.size(), 1u);
    EXPECT_EQ(Cache.BakeCount(), 1u);
    EXPECT_EQ(Leases, 1u);
    EXPECT_TRUE(record.Meshes[0].LocalBounds.IsValid());

    Cache.Ensure(Registry, Brush, Evaluate(box, {}), Default);
    EXPECT_EQ(Cache.BakeCount(), 1u);
}

TEST_F(BrushBakeCacheTest, ArrayBakesOneMeshHoweverManyCopies)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushBakedRecord& record = Cache.Ensure(Registry, Brush, Evaluate(box, { Array(100) }), Default);
    EXPECT_EQ(record.Meshes.size(), 1u);
    EXPECT_EQ(Cache.BakeCount(), 1u);
}

TEST_F(BrushBakeCacheTest, CountAndSpacingEditsBakeNothing)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const StaticMeshHandle first = Cache.Ensure(Registry, Brush, Evaluate(box, { Array(2) }), Default).Meshes[0].Handle;
    for (int count = 3; count <= 100; ++count)
    {
        BrushModifier array = Array(count);
        std::get<ArrayModifier>(array.Params).Spacing = static_cast<float>(count);
        const BrushBakedRecord& record = Cache.Ensure(Registry, Brush, Evaluate(box, { array }), Default);
        EXPECT_EQ(record.Meshes[0].Handle, first);
    }
    EXPECT_EQ(Cache.BakeCount(), 1u);
    EXPECT_EQ(Cache.DestroyCount(), 0u);
}

TEST_F(BrushBakeCacheTest, MirrorThenArrayBakesTwoMeshesAndAPlaneEditRebakesOnlyTheCopy)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushBakedRecord& record =
        Cache.Ensure(Registry, Brush, Evaluate(box, { Mirror(3.0f), Array(4) }), Default);
    ASSERT_EQ(record.Meshes.size(), 2u);
    EXPECT_EQ(Cache.BakeCount(), 2u);
    const StaticMeshHandle source = record.Meshes[0].Handle;
    const StaticMeshHandle copy = record.Meshes[1].Handle;

    const BrushBakedRecord& moved =
        Cache.Ensure(Registry, Brush, Evaluate(box, { Mirror(5.0f), Array(4) }), Default);
    ASSERT_EQ(moved.Meshes.size(), 2u);
    EXPECT_EQ(moved.Meshes[0].Handle, source);
    EXPECT_NE(moved.Meshes[1].Handle, copy);
    EXPECT_EQ(Cache.BakeCount(), 3u);
    ASSERT_EQ(Destroyed.size(), 1u);
    EXPECT_EQ(Destroyed[0], copy);
}

TEST_F(BrushBakeCacheTest, SourceEditRebakesEverythingAndRetiresTheOld)
{
    BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    Cache.Ensure(Registry, Brush, Evaluate(box, { Mirror(3.0f) }), Default);
    box.Vertices[0].Position.Y += 0.25f;
    const BrushBakedRecord& record = Cache.Ensure(Registry, Brush, Evaluate(box, { Mirror(3.0f) }), Default);
    EXPECT_EQ(record.Meshes.size(), 2u);
    EXPECT_EQ(Cache.BakeCount(), 4u);
    EXPECT_EQ(Cache.DestroyCount(), 2u);
    EXPECT_EQ(Releases, 2u);
}

TEST_F(BrushBakeCacheTest, RemovingTheMirrorRetiresItsMeshAndKeepsTheSource)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const StaticMeshHandle source =
        Cache.Ensure(Registry, Brush, Evaluate(box, { Mirror(3.0f) }), Default).Meshes[0].Handle;
    const BrushBakedRecord& record = Cache.Ensure(Registry, Brush, Evaluate(box, {}), Default);
    ASSERT_EQ(record.Meshes.size(), 1u);
    EXPECT_EQ(record.Meshes[0].Handle, source);
    EXPECT_EQ(Cache.BakeCount(), 2u);
    EXPECT_EQ(Cache.DestroyCount(), 1u);
}

TEST_F(BrushBakeCacheTest, DefaultMaterialChangeRebakesAndChangesTheContentHash)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const std::uint64_t before = Cache.Ensure(Registry, Brush, Evaluate(box, {}), Default).Meshes[0].ContentHash;
    const AssetRef other{ AssetType::Material, "asset://materials/dev/blue.smat" };
    const BrushBakedRecord& record = Cache.Ensure(Registry, Brush, Evaluate(box, {}), other);
    EXPECT_NE(record.Meshes[0].ContentHash, before);
    EXPECT_EQ(Cache.BakeCount(), 2u);
    EXPECT_EQ(Cache.DestroyCount(), 1u);
}

TEST_F(BrushBakeCacheTest, BudgetPolicyChangeWithTheSameMeshesBakesNothing)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    Cache.Ensure(Registry, Brush, Evaluate(box, { Array(50) }), Default);
    const BrushEvaluated limited =
        EvaluateBrushModifiers(box, { Array(50) }, BrushEvaluationPolicy::Interactive(10));
    EXPECT_EQ(limited.Status, BrushEvaluationStatus::PieceBudgetExceeded);
    Cache.Ensure(Registry, Brush, limited, Default);
    EXPECT_EQ(Cache.BakeCount(), 1u);
}

TEST_F(BrushBakeCacheTest, SweepKeepsExistingBrushesDropsDeletedOnesAndClosedDocuments)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const RegistryId otherRegistry{ 2, 1 };
    Cache.Ensure(Registry, Brush, Evaluate(box, {}), Default);
    Cache.Ensure(Registry, BrushId{ 8 }, Evaluate(box, {}), Default);
    Cache.Ensure(otherRegistry, Brush, Evaluate(box, {}), Default);
    EXPECT_EQ(Cache.RecordCount(), 3u);

    // Hidden is not a sweep criterion: the predicate is existence only.
    const RegistryId open[] = { Registry };
    Cache.Sweep(open, [&](RegistryId, BrushId id) { return id == Brush; });
    EXPECT_EQ(Cache.RecordCount(), 1u);
    EXPECT_EQ(Cache.DestroyCount(), 2u);

    Cache.Sweep(open, [&](RegistryId, BrushId) { return true; });
    EXPECT_EQ(Cache.RecordCount(), 1u);
    EXPECT_EQ(Cache.DestroyCount(), 2u);
}

TEST_F(BrushBakeCacheTest, ClearReleasesEveryHandleAndLease)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    Cache.Ensure(Registry, Brush, Evaluate(box, { Mirror(2.0f) }), Default);
    Cache.Clear();
    EXPECT_EQ(Cache.RecordCount(), 0u);
    EXPECT_EQ(Cache.DestroyCount(), 2u);
    EXPECT_EQ(Releases, Leases);
}
