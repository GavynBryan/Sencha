// Stage 1 collection contract: a snapshot captures only what the resolved graph
// needs (lazy), and step selection lives in the request's graph, not in stored
// Run* booleans or a captured field being empty.

#include "document/DocumentCook.h"
#include "document/DocumentCookRequest.h"
#include "document/DocumentCookSnapshot.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <project/CookProfile.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace
{
namespace fs = std::filesystem;

class DocumentCookSnapshotTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_snap_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
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

    // A floor slab plus a Direct bake light: a Full cook gathers charts and bake
    // lights from this; a collision-only cook needs neither.
    void AuthorFloorWithLight(EditorDocument& doc)
    {
        doc.SetDefaultMaterial(
            AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        doc.GetScene().CreateBrush(Vec3d{ 0, 0, 0 }, Vec3d{ 4.0, 0.25, 4.0 });

        const EntityId light = doc.GetScene().CreateEntity(Vec3d{ 0, 4, 0 });
        PointLightComponent lc{};
        lc.Intensity = 40.0f;
        lc.Range = 12.0f;
        lc.BakeContribution = LightBakeContribution::Direct;
        doc.GetScene().GetRegistry().Components.AddComponent(light, lc);
    }

    static CookProfile CollisionOnlyProfile()
    {
        CookProfile profile;
        profile.Id = "collision_only_test";
        profile.Name = "Collision Only Test";
        profile.TargetSteps = { std::string(CookStepIds::Collision) };
        return profile;
    }

    fs::path Root;
    LoggingProvider Logging;
};
} // namespace

TEST_F(DocumentCookSnapshotTest, CollisionOnlySnapshotSkipsLightingInputs)
{
    EditorDocument doc(Logging);
    AuthorFloorWithLight(doc);

    const CookProfile collisionOnly = CollisionOnlyProfile();
    const std::optional<DocumentCookInput> input = CollectDocumentCookInput(
        doc, Root, 16.0, Logging, nullptr, {}, {},
        DocumentCookOptions{ .Profile = &collisionOnly });
    ASSERT_TRUE(input.has_value());

    // Selection lives in the graph.
    EXPECT_TRUE(input->Request().Selects(CookStepIds::Collision));
    EXPECT_FALSE(input->Request().Selects(CookStepIds::DirectLightmap));
    EXPECT_FALSE(input->Request().Selects(CookStepIds::IrradianceProbes));

    // Collection stayed lazy: no lighting inputs gathered though a light exists.
    EXPECT_TRUE(input->Snapshot().BakeLights.empty());
    EXPECT_TRUE(input->Snapshot().Charts.Extents.empty());
    EXPECT_TRUE(input->Snapshot().Placements.empty());
    EXPECT_TRUE(input->Snapshot().ProbeVolumes.empty());

    // Structure is captured regardless of the lighting selection.
    EXPECT_FALSE(input->Snapshot().Brushes.empty());
    EXPECT_TRUE(input->Snapshot().PassthroughScene.IsObject());
}

TEST_F(DocumentCookSnapshotTest, FullSnapshotGathersLightingInputs)
{
    EditorDocument doc(Logging);
    AuthorFloorWithLight(doc);

    // Default profile is Full: it selects lighting, so collection gathers the
    // bake light and builds charts.
    const std::optional<DocumentCookInput> input =
        CollectDocumentCookInput(doc, Root, 16.0, Logging);
    ASSERT_TRUE(input.has_value());

    EXPECT_TRUE(input->Request().Selects(CookStepIds::DirectLightmap));
    EXPECT_FALSE(input->Snapshot().BakeLights.empty());
    EXPECT_FALSE(input->Snapshot().Charts.Extents.empty());
}
