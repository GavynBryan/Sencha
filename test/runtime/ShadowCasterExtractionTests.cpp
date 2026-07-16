#include <gtest/gtest.h>

#include <render/MaterialCache.h>
#include <render/ShadowCasterExtractionSystem.h>
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
