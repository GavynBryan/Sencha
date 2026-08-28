// Evidence generator: cooks a probe-volume validation scene for a live GPU
// run (SceneViewer +map levels/probe_spike). Two sealed rooms share a wall;
// a Direct light fills the left room, an Indirect light the right, and one
// IrradianceVolume spans both, so a run shows probe ambient tracking each
// room's light while the runtime exercises the full residency path
// (.sprobe read, 3D uploads, binding-2 slot writes, per-fragment selection).
//
// Skipped unless SENCHA_PROBE_SPIKE_ROOT names the assets root to cook into.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <string>

namespace
{
namespace fs = std::filesystem;
}

TEST(ProbeSpike, Generate)
{
    const char* rootEnv = std::getenv("SENCHA_PROBE_SPIKE_ROOT");
    if (rootEnv == nullptr)
        GTEST_SKIP() << "set SENCHA_PROBE_SPIKE_ROOT to cook the probe spike scene";
    const fs::path root(rootEnv);

    RegisterDocumentSerializers();
    LoggingProvider logging;
    EditorDocument doc(logging);
    doc.SetDefaultMaterial(
        AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });

    // Two rooms as floor/ceiling slabs plus perimeter walls and the shared
    // divider; brushes overlap at corners, the kyusu-authored norm.
    doc.GetScene().CreateBrush(Vec3d{ 0.0f, -0.25f, 0.0f }, Vec3d{ 12.5, 0.25, 6.5 });
    doc.GetScene().CreateBrush(Vec3d{ 0.0f, 4.25f, 0.0f }, Vec3d{ 12.5, 0.25, 6.5 });
    doc.GetScene().CreateBrush(Vec3d{ -12.25f, 2.0f, 0.0f }, Vec3d{ 0.25, 2.0, 6.5 });
    doc.GetScene().CreateBrush(Vec3d{ 12.25f, 2.0f, 0.0f }, Vec3d{ 0.25, 2.0, 6.5 });
    doc.GetScene().CreateBrush(Vec3d{ 0.0f, 2.0f, -6.25f }, Vec3d{ 12.5, 2.0, 0.25 });
    doc.GetScene().CreateBrush(Vec3d{ 0.0f, 2.0f, 6.25f }, Vec3d{ 12.5, 2.0, 0.25 });
    // The shared divider, with a doorway gap so a viewer can fly through.
    doc.GetScene().CreateBrush(Vec3d{ 0.0f, 2.0f, -3.75f }, Vec3d{ 0.25, 2.0, 2.75 });
    doc.GetScene().CreateBrush(Vec3d{ 0.0f, 3.5f, 0.0f }, Vec3d{ 0.25, 0.5, 1.25 });
    doc.GetScene().CreateBrush(Vec3d{ 0.0f, 2.0f, 3.75f }, Vec3d{ 0.25, 2.0, 2.75 });

    // Left room: warm Direct light (baked into the lightmap AND bounced into
    // probes; costs nothing at runtime).
    {
        const EntityId entity = doc.GetScene().CreateEntity(Vec3d{ -6.0f, 3.0f, 0.0f });
        PointLightComponent light{};
        light.Color = Vec3d{ 1.0f, 0.8f, 0.5f };
        light.Intensity = 25.0f;
        light.Range = 18.0f;
        light.BakeContribution = LightBakeContribution::Direct;
        doc.GetScene().GetRegistry().Components.AddComponent(entity, light);
    }
    // Right room: cool Indirect light (dynamic direct at runtime, bounce
    // baked into probes; the authoring contract's showcase).
    {
        const EntityId entity = doc.GetScene().CreateEntity(Vec3d{ 6.0f, 3.0f, 0.0f });
        PointLightComponent light{};
        light.Color = Vec3d{ 0.45f, 0.65f, 1.0f };
        light.Intensity = 25.0f;
        light.Range = 18.0f;
        light.BakeContribution = LightBakeContribution::Indirect;
        doc.GetScene().GetRegistry().Components.AddComponent(entity, light);
    }
    // One volume spanning both rooms at 1m cells.
    {
        const EntityId entity = doc.GetScene().CreateEntity(Vec3d{ 0.0f, 2.0f, 0.0f });
        IrradianceVolumeComponent volume{};
        volume.HalfExtents = Vec3d(12.0f, 2.0f, 6.0f);
        volume.CellSize = 1.0f;
        doc.GetScene().GetRegistry().Components.AddComponent(entity, volume);
    }

    const fs::path materialPath = root / "materials/dev/gray.smat";
    fs::create_directories(materialPath.parent_path());
    if (!fs::exists(materialPath))
        std::ofstream(materialPath, std::ios::trunc) << "{}";

    const fs::path levelPath = root / "levels/probe_spike.sscene";
    fs::create_directories(levelPath.parent_path());
    ASSERT_TRUE(doc.SaveAs(levelPath.generic_string()));

    const DocumentCookResult result =
        CookDocument(levelPath, root, /*cellSize*/ 64.0);
    ASSERT_TRUE(result.Success) << result.Error;
    EXPECT_EQ(result.DirectLightCount, 1u);
    EXPECT_EQ(result.ProbeVolumeCount, 1u);
    EXPECT_GT(result.ProbeCount, 0u);
}
