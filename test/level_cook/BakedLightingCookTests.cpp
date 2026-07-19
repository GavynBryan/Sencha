// End-to-end: a brush level with a LightBakeContribution::Direct light cooks
// to a per-zone lightmap atlas (.stex artifact + ZoneLightmap scene entity +
// vertex lightmap UVs), a None light leaves no atlas, and every bake input
// that changes the lightmap restales the cook hash. Headless: no AssetSystem,
// no graphics.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "brush/BrushMesh.h"

#include <assets/static_mesh/MeshLoader.h>
#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>
#include <render/static_mesh/MeshGeometry.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
namespace fs = std::filesystem;

class BakedLightingCookTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_bakelight_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root);
        const fs::path material = Root / "materials/dev/gray.smat";
        fs::create_directories(material.parent_path());
        std::ofstream(material, std::ios::trunc) << "{}";
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    // A floor slab at the origin plus a point light above it, authored with the
    // given bake contribution. Returns the saved level path.
    fs::path AuthorFloorWithLight(LightBakeContribution contribution,
                                  Vec3d lightPosition = Vec3d{ 0, 4, 0 })
    {
        EditorDocument doc(Logging);
        doc.SetDefaultMaterial(
            AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        doc.GetScene().CreateBrush(Vec3d{ 0, 0, 0 }, Vec3d{ 4.0, 0.25, 4.0 });

        const EntityId lightEntity = doc.GetScene().CreateEntity(lightPosition);
        PointLightComponent light{};
        light.Color = Vec3d(1.0f, 1.0f, 1.0f);
        light.Intensity = 40.0f;
        light.Range = 12.0f;
        light.BakeContribution = contribution;
        doc.GetScene().GetRegistry().Components.AddComponent(lightEntity, light);

        const fs::path levelPath = Root / "levels/test.json";
        fs::create_directories(levelPath.parent_path());
        EXPECT_TRUE(doc.SaveAs(levelPath.generic_string()));
        return levelPath;
    }

    // Two coplanar quads sharing an edge, plus a Direct light. Toggling the
    // shared edge soft merges the two per-face charts into one without moving
    // a single vertex byte: the staleness case only the chart hash catches.
    fs::path AuthorCoplanarPair(bool sharedEdgeSoft)
    {
        BrushMesh mesh;
        mesh.Vertices = {
            BrushVertex{ Vec3d{ -2, 0, -1 } }, BrushVertex{ Vec3d{ 0, 0, -1 } },
            BrushVertex{ Vec3d{ 2, 0, -1 } },  BrushVertex{ Vec3d{ -2, 0, 1 } },
            BrushVertex{ Vec3d{ 0, 0, 1 } },   BrushVertex{ Vec3d{ 2, 0, 1 } },
        };
        mesh.Faces = {
            BrushFace{ { 0, 1, 4, 3 }, Vec3d{ 0, 1, 0 }, {} },
            BrushFace{ { 1, 2, 5, 4 }, Vec3d{ 0, 1, 0 }, {} },
        };
        if (sharedEdgeSoft)
            BrushSetEdgeSoft(mesh, 1, 4, true);

        EditorDocument doc(Logging);
        doc.SetDefaultMaterial(
            AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        doc.GetScene().CreateBrushFromMesh(Transform3f{}, std::move(mesh));

        const EntityId lightEntity = doc.GetScene().CreateEntity(Vec3d{ 0, 3, 0 });
        PointLightComponent light{};
        light.Intensity = 20.0f;
        light.Range = 10.0f;
        light.BakeContribution = LightBakeContribution::Direct;
        doc.GetScene().GetRegistry().Components.AddComponent(lightEntity, light);

        const fs::path levelPath = Root / "levels/test.json";
        fs::create_directories(levelPath.parent_path());
        EXPECT_TRUE(doc.SaveAs(levelPath.generic_string()));
        return levelPath;
    }

    bool AnyCellVertexHasLightmapUv()
    {
        MeshLoader loader(Logging);
        for (const fs::directory_entry& entry :
             fs::recursive_directory_iterator(Root / ".cooked/levels/test"))
        {
            if (entry.path().extension() != ".smesh")
                continue;
            MeshGeometry geometry;
            EXPECT_TRUE(loader.LoadFromFile(entry.path().generic_string(), geometry));
            for (const StaticMeshVertex& vertex : geometry.Vertices)
                if (vertex.LightmapU != 0 || vertex.LightmapV != 0)
                    return true;
        }
        return false;
    }

    bool CookedSceneNamesZoneLightmap()
    {
        std::ifstream file(Root / ".cooked/levels/test.cooked.json");
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str().find("ZoneLightmap") != std::string::npos;
    }

    fs::path Root;
    LoggingProvider Logging;
};
} // namespace

TEST_F(BakedLightingCookTest, DirectLightBakesAtlasAndVertexUvs)
{
    const fs::path levelPath = AuthorFloorWithLight(LightBakeContribution::Direct);
    const DocumentCookResult result = CookDocument(levelPath, Root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;
    ASSERT_FALSE(result.GeneratedMeshPaths.empty());

    EXPECT_EQ(result.DirectLightCount, 1u);
    EXPECT_GT(result.LightmapAtlasWidth, 0u);
    EXPECT_GT(result.LightmapAtlasHeight, 0u);
    EXPECT_TRUE(fs::exists(Root / ".cooked/levels/test/lightmap.stex"));
    EXPECT_TRUE(CookedSceneNamesZoneLightmap());
    EXPECT_TRUE(AnyCellVertexHasLightmapUv());
}

TEST_F(BakedLightingCookTest, NonBakedLightLeavesNoAtlas)
{
    const fs::path levelPath = AuthorFloorWithLight(LightBakeContribution::None);
    const DocumentCookResult result = CookDocument(levelPath, Root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    EXPECT_EQ(result.DirectLightCount, 0u);
    EXPECT_EQ(result.LightmapAtlasWidth, 0u);
    EXPECT_FALSE(fs::exists(Root / ".cooked/levels/test/lightmap.stex"));
    EXPECT_FALSE(CookedSceneNamesZoneLightmap());
    EXPECT_FALSE(AnyCellVertexHasLightmapUv());
}

TEST_F(BakedLightingCookTest, RestalesOnLightMove)
{
    const uint64_t before = CookDocument(
        AuthorFloorWithLight(LightBakeContribution::Direct, Vec3d{ 0, 4, 0 }),
        Root, 16.0).ContentHash;
    const uint64_t after = CookDocument(
        AuthorFloorWithLight(LightBakeContribution::Direct, Vec3d{ 1, 4, 0 }),
        Root, 16.0).ContentHash;
    EXPECT_NE(before, after);
}

TEST_F(BakedLightingCookTest, RestalesOnLightmapTuningOnlyWhenLightsExist)
{
    LightmapCookParams fine{};
    LightmapCookParams coarse{};
    coarse.LuxelSize = 0.5f;
    LightmapCookParams wideCone{};
    wideCone.ConeDegrees = 80.0f;

    const fs::path lit = AuthorFloorWithLight(LightBakeContribution::Direct);
    const uint64_t litFine = CookDocument(lit, Root, 16.0, nullptr, nullptr, fine).ContentHash;
    const uint64_t litCoarse = CookDocument(lit, Root, 16.0, nullptr, nullptr, coarse).ContentHash;
    const uint64_t litCone = CookDocument(lit, Root, 16.0, nullptr, nullptr, wideCone).ContentHash;
    EXPECT_NE(litFine, litCoarse);
    EXPECT_NE(litFine, litCone);

    const fs::path unlit = AuthorFloorWithLight(LightBakeContribution::None);
    const uint64_t unlitFine = CookDocument(unlit, Root, 16.0, nullptr, nullptr, fine).ContentHash;
    const uint64_t unlitCoarse = CookDocument(unlit, Root, 16.0, nullptr, nullptr, coarse).ContentHash;
    EXPECT_EQ(unlitFine, unlitCoarse);
}

TEST_F(BakedLightingCookTest, RestalesOnCoplanarSoftEdgeToggle)
{
    // The two-quad sheet is geometrically identical either way; only the
    // chart grouping changes, so this passes only if chart identity is part
    // of the cook hash.
    const uint64_t hard = CookDocument(AuthorCoplanarPair(false), Root, 16.0).ContentHash;
    const uint64_t soft = CookDocument(AuthorCoplanarPair(true), Root, 16.0).ContentHash;
    EXPECT_NE(hard, soft);
}
