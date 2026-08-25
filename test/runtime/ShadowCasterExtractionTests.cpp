#include <gtest/gtest.h>

#include <render/MaterialCache.h>
#include <render/extract/ShadowCasterExtractionSystem.h>
#include <render/ShadowCasterSet.h>
#include <render/StaticMeshComponent.h>

#include <cstdint>
#include <vector>

namespace
{
    constexpr StaticMeshHandle kMeshHandle{ 7, 3 };

    GpuStaticMesh MakeTwoSectionMesh()
    {
        GpuStaticMesh mesh;
        mesh.LocalBounds = Aabb3d(Vec3d(-1.0f, -1.0f, -1.0f), Vec3d(1.0f, 1.0f, 1.0f));
        mesh.Sections.push_back({ .IndexOffset = 0, .IndexCount = 3, .MaterialSlot = 0 });
        mesh.Sections.push_back({ .IndexOffset = 3, .IndexCount = 3, .MaterialSlot = 1 });
        return mesh;
    }

    MaterialHandle MakeMaterial(MaterialCache& materials,
                                bool castShadows,
                                bool doubleSided = false)
    {
        Material material;
        material.CastShadows = castShadows;
        material.DoubleSided = doubleSided;
        return materials.Create(material);
    }

    StaticMeshComponent MakeRenderer()
    {
        StaticMeshComponent renderer;
        renderer.Mesh = kMeshHandle;
        return renderer;
    }
}

TEST(ShadowCasterGather, SkipsSectionsWhoseMaterialDoesNotCastShadows)
{
    MaterialCache materials;
    const GpuStaticMesh mesh = MakeTwoSectionMesh();
    const std::vector<MaterialHandle> sectionMaterials{
        MakeMaterial(materials, true),
        MakeMaterial(materials, false),
    };

    ShadowCasterSet casters;
    AppendShadowCasters(MakeRenderer(), mesh, sectionMaterials, materials,
                        Mat4::Identity(), casters);

    ASSERT_EQ(casters.Items.size(), 1u);
    EXPECT_EQ(casters.Items[0].SectionIndex, 0u);
    EXPECT_EQ(casters.Items[0].Mesh, kMeshHandle);
    EXPECT_EQ(casters.Items[0].Material, sectionMaterials[0]);
}

TEST(ShadowCasterGather, SectionMaskFiltersSections)
{
    MaterialCache materials;
    const GpuStaticMesh mesh = MakeTwoSectionMesh();
    const std::vector<MaterialHandle> sectionMaterials{
        MakeMaterial(materials, true),
        MakeMaterial(materials, true),
    };

    StaticMeshComponent renderer = MakeRenderer();
    renderer.SectionMask = 0b10u;

    ShadowCasterSet casters;
    AppendShadowCasters(renderer, mesh, sectionMaterials, materials,
                        Mat4::Identity(), casters);

    ASSERT_EQ(casters.Items.size(), 1u);
    EXPECT_EQ(casters.Items[0].SectionIndex, 1u);
}

TEST(ShadowCasterGather, PropagatesDoubleSidedFromTheMaterial)
{
    MaterialCache materials;
    const GpuStaticMesh mesh = MakeTwoSectionMesh();
    const std::vector<MaterialHandle> sectionMaterials{
        MakeMaterial(materials, true, false),
        MakeMaterial(materials, true, true),
    };

    ShadowCasterSet casters;
    AppendShadowCasters(MakeRenderer(), mesh, sectionMaterials, materials,
                        Mat4::Identity(), casters);

    ASSERT_EQ(casters.Items.size(), 2u);
    EXPECT_FALSE(casters.Items[0].DoubleSided);
    EXPECT_TRUE(casters.Items[1].DoubleSided);
}

TEST(ShadowCasterGather, UnderBoundMaterialSetFallsBackToLastMaterial)
{
    MaterialCache materials;
    const GpuStaticMesh mesh = MakeTwoSectionMesh();
    const std::vector<MaterialHandle> sectionMaterials{
        MakeMaterial(materials, true),
    };

    ShadowCasterSet casters;
    AppendShadowCasters(MakeRenderer(), mesh, sectionMaterials, materials,
                        Mat4::Identity(), casters);

    ASSERT_EQ(casters.Items.size(), 2u);
    EXPECT_EQ(casters.Items[1].Material, sectionMaterials[0]);
}

TEST(ShadowCasterGather, EmptyMaterialSetAppendsNothing)
{
    MaterialCache materials;
    const GpuStaticMesh mesh = MakeTwoSectionMesh();

    ShadowCasterSet casters;
    AppendShadowCasters(MakeRenderer(), mesh, {}, materials,
                        Mat4::Identity(), casters);

    EXPECT_TRUE(casters.Items.empty());
}

TEST(ShadowCasterGather, ComponentCastShadowsSwitchRemovesEveryCaster)
{
    MaterialCache materials;
    const GpuStaticMesh mesh = MakeTwoSectionMesh();
    const std::vector<MaterialHandle> sectionMaterials{
        MakeMaterial(materials, true),
        MakeMaterial(materials, true),
    };

    StaticMeshComponent renderer = MakeRenderer();
    renderer.CastShadows = false;

    ShadowCasterSet casters;
    AppendShadowCasters(renderer, mesh, sectionMaterials, materials,
                        Mat4::Identity(), casters);

    EXPECT_TRUE(casters.Items.empty());
}

TEST(ShadowCasterGather, InvisibleInstancesDoNotCast)
{
    MaterialCache materials;
    const GpuStaticMesh mesh = MakeTwoSectionMesh();
    const std::vector<MaterialHandle> sectionMaterials{
        MakeMaterial(materials, true),
        MakeMaterial(materials, true),
    };

    StaticMeshComponent renderer = MakeRenderer();
    renderer.Visible = false;

    ShadowCasterSet casters;
    AppendShadowCasters(renderer, mesh, sectionMaterials, materials,
                        Mat4::Identity(), casters);

    EXPECT_TRUE(casters.Items.empty());
}

TEST(ShadowCasterGather, GathersIndependentlyOfAnyCameraPosition)
{
    // The gather takes no camera: an instance far outside any plausible view
    // frustum still casts, because per-view culling happens against the
    // light's frustum at depth-render time.
    MaterialCache materials;
    const GpuStaticMesh mesh = MakeTwoSectionMesh();
    const std::vector<MaterialHandle> sectionMaterials{
        MakeMaterial(materials, true),
        MakeMaterial(materials, true),
    };

    Mat4 farAway = Mat4::Identity();
    farAway[0][3] = 10000.0f;
    farAway[1][3] = -2500.0f;
    farAway[2][3] = 10000.0f;

    ShadowCasterSet casters;
    AppendShadowCasters(MakeRenderer(), mesh, sectionMaterials, materials,
                        farAway, casters);

    ASSERT_EQ(casters.Items.size(), 2u);
    EXPECT_EQ(casters.Items[0].WorldMatrix, farAway);
    EXPECT_NEAR(casters.Items[0].WorldBounds.Min.X, 9999.0f, 1.0e-3f);
    EXPECT_NEAR(casters.Items[0].WorldBounds.Max.X, 10001.0f, 1.0e-3f);
    EXPECT_NEAR(casters.Items[0].WorldBounds.Min.Y, -2501.0f, 1.0e-3f);
}

// The change table's fields are the caster diff's identity: a site that
// assembled a record by hand and dropped one -- the quantized bounds, the
// material state hash -- would stop invalidating shadows for changes in
// whatever it dropped, and only in that host. Three sites build these (runtime
// extraction, the editor's cooked brush cells, its placed entities), so the
// assembly is one kernel and this is what pins it.
TEST(ShadowCasterRecord, CarriesEveryFieldTheDiffComparesOn)
{
    ShadowCasterSet casters;
    const ShadowCasterGatherResult gathered{
        .EffectiveSectionMask = 0b1011,
        .MaterialStateHash = 0xfeedfaceull,
        .WorldBounds = Aabb3d(Vec3d(-1.5, -2.0, -0.5), Vec3d(3.25, 1.0, 4.0)),
    };
    const RenderEntityKey key{ .Entity = EntityId{ .Index = 7, .Generation = 2 } };
    const StaticMeshHandle mesh{ 11, 1 };
    const MaterialSetHandle materials{ 5, 1 };

    AppendShadowCasterRecord(casters, key, mesh, materials, gathered);

    ASSERT_EQ(casters.Records.size(), 1u);
    const ShadowCasterRecord& record = casters.Records.front();
    EXPECT_EQ(record.Key.Entity.Index, key.Entity.Index);
    EXPECT_EQ(record.State.Mesh, mesh);
    EXPECT_EQ(record.State.Materials, materials);
    EXPECT_EQ(record.State.EffectiveShadowSectionMask, gathered.EffectiveSectionMask);
    EXPECT_EQ(record.State.ShadowMaterialStateHash, gathered.MaterialStateHash);
    // Bounds are stored quantized, so a sub-threshold jitter does not read as
    // a change; the raw bounds would.
    EXPECT_EQ(record.State.WorldBounds,
              QuantizeShadowCasterBounds(gathered.WorldBounds));
}

TEST(ShadowCasterRecord, AppendsInCallOrder)
{
    ShadowCasterSet casters;
    const ShadowCasterGatherResult gathered{ .EffectiveSectionMask = 1 };
    AppendShadowCasterRecord(casters, RenderEntityKey{ .Entity = EntityId{ .Index = 1 } },
                             StaticMeshHandle{ 1, 1 }, MaterialSetHandle{}, gathered);
    AppendShadowCasterRecord(casters, RenderEntityKey{ .Entity = EntityId{ .Index = 2 } },
                             StaticMeshHandle{ 2, 1 }, MaterialSetHandle{}, gathered);

    ASSERT_EQ(casters.Records.size(), 2u);
    EXPECT_EQ(casters.Records[0].State.Mesh, (StaticMeshHandle{ 1, 1 }));
    EXPECT_EQ(casters.Records[1].State.Mesh, (StaticMeshHandle{ 2, 1 }));
}
