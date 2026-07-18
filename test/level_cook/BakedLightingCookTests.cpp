// End-to-end: a brush level with a LightBakeContribution::Direct light cooks
// with the light's direct diffuse baked into the cell mesh vertices, and a
// None light leaves the channel neutral. Headless: no AssetSystem, no graphics.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

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
    fs::path AuthorFloorWithLight(LightBakeContribution contribution)
    {
        EditorDocument doc(Logging);
        doc.SetDefaultMaterial(
            AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        doc.GetScene().CreateBrush(Vec3d{ 0, 0, 0 }, Vec3d{ 4.0, 0.25, 4.0 });

        const EntityId lightEntity = doc.GetScene().CreateEntity(Vec3d{ 0, 4, 0 });
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

    bool AnyCellVertexBaked()
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
                if (vertex.BakedDirect != 0)
                    return true;
        }
        return false;
    }

    fs::path Root;
    LoggingProvider Logging;
};
} // namespace

TEST_F(BakedLightingCookTest, DirectLightBakesIntoCellVertices)
{
    const fs::path levelPath = AuthorFloorWithLight(LightBakeContribution::Direct);
    const DocumentCookResult result = CookDocument(levelPath, Root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;
    ASSERT_FALSE(result.GeneratedMeshPaths.empty());

    EXPECT_TRUE(AnyCellVertexBaked());
}

TEST_F(BakedLightingCookTest, NonBakedLightLeavesChannelNeutral)
{
    const fs::path levelPath = AuthorFloorWithLight(LightBakeContribution::None);
    const DocumentCookResult result = CookDocument(levelPath, Root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    // A None light stays dynamic; nothing is baked, so the channel is neutral.
    EXPECT_FALSE(AnyCellVertexBaked());
}
