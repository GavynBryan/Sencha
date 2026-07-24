// Evidence generator (docs/plans/evidence/baked-direct/): cooks the baked
// counterpart of the light-stress measurement scene. 96 small accent point
// lights authored LightBakeContribution::Direct over a floor brush, so every
// light bakes into cell vertices and none enters the runtime forward set. The
// dynamic version of this regime caps at 64 visible lights and silently drops
// the rest (docs/plans/evidence/point-light-cost/); the baked version must
// render all 96 pools with LightsVisible == 0 and nothing dropped.
//
// Skipped unless SENCHA_BAKED_STRESS_ROOT names the assets root to cook into.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

TEST(BakedLightingStress, Generate)
{
    const char* rootEnv = std::getenv("SENCHA_BAKED_STRESS_ROOT");
    if (rootEnv == nullptr)
        GTEST_SKIP();
    namespace fs = std::filesystem;
    const fs::path root = rootEnv;

    LoggingProvider logging;
    RegisterDocumentSerializers();

    EditorDocument doc(logging);
    doc.SetDefaultMaterial(
        AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
    // One large floor brush centered under the SceneViewer default camera's
    // frustum (camera at (0,3,10) looking -Z), matching the measurement rig.
    doc.GetScene().CreateBrush(Vec3d{ 0, 0, -5 }, Vec3d{ 16.0, 0.25, 16.0 });

    // 96 small-range accent lights on a grid filling the near frustum, the
    // many-small-lights regime from the measurement (N > kMaxForwardLights).
    constexpr int kCols = 8;
    constexpr int kRows = 12;
    const std::array<Vec3d, 4> palette{
        Vec3d{ 0.05f, 0.10f, 1.00f }, Vec3d{ 0.99f, 0.65f, 0.22f },
        Vec3d{ 0.55f, 0.15f, 0.95f }, Vec3d{ 0.10f, 0.85f, 0.95f } };
    for (int i = 0; i < kCols * kRows; ++i)
    {
        const int col = i % kCols;
        const int row = i / kCols;
        const float x = -5.5f + 11.0f * static_cast<float>(col) / (kCols - 1);
        const float z = 4.0f - 18.0f * static_cast<float>(row) / (kRows - 1);
        const EntityId entity = doc.GetScene().CreateEntity(Vec3d{ x, 1.4f, z });
        PointLightComponent light{};
        light.Color = palette[static_cast<std::size_t>(i) % palette.size()];
        light.Intensity = 10.0f;
        light.Range = 3.0f;
        light.BakeContribution = LightBakeContribution::Direct;
        doc.GetScene().GetRegistry().Components.AddComponent(entity, light);
    }

    const fs::path levelPath = root / "levels/baked_stress_96.json";
    fs::create_directories(levelPath.parent_path());
    ASSERT_TRUE(doc.SaveAs(levelPath.generic_string()));

    const DocumentCookResult result = CookDocument(
        levelPath, root, /*cellSize*/ 64.0, nullptr, nullptr, LightingCookParams{});
    ASSERT_TRUE(result.Success) << result.Error;
    EXPECT_EQ(result.DirectLightCount, static_cast<std::size_t>(kCols * kRows));
    EXPECT_GT(result.LightmapAtlasWidth, 0u);
    EXPECT_GT(result.LightmapAtlasHeight, 0u);
}
