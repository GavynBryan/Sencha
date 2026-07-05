#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/PortalGeometry.h"
#include "document/commands/CreateEntityCommand.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>

#include <string>

namespace
{

class PortalTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    LoggingProvider Logging;   // sink-less: silent
};

} // namespace

TEST_F(PortalTest, PortalComponentRoundTripsThroughSceneJson)
{
    EditorDocument document(Logging);
    EditorScene& scene = document.GetScene();
    const TransitionId linked{ 0xabcdef0123456789ull };

    const EntityId linkedPortal = scene.CreateBrush(Vec3d{ 0, 0, 0 });
    scene.GetRegistry().Components.AddComponent(linkedPortal, PortalComponent{ linked });
    const EntityId unlinkedPortal = scene.CreateBrush(Vec3d{ 4, 0, 0 });
    scene.GetRegistry().Components.AddComponent(unlinkedPortal, PortalComponent{});

    EditorDocument restored(Logging);
    ASSERT_TRUE(restored.LoadFromJson(document.ToJson()));

    size_t linkedCount = 0;
    size_t unlinkedCount = 0;
    for (EntityId entity : restored.GetScene().GetAllEntities())
    {
        const PortalComponent* portal = restored.GetScene().TryGetPortal(entity);
        if (portal == nullptr)
            continue;
        if (portal->Transition == linked)
            ++linkedCount;
        else if (!portal->Transition.IsValid())
            ++unlinkedCount;
    }
    EXPECT_EQ(linkedCount, 1u);
    EXPECT_EQ(unlinkedCount, 1u);
}

TEST_F(PortalTest, PortalSurvivesCaptureRestore)
{
    EditorDocument document(Logging);
    EditorScene& scene = document.GetScene();
    const TransitionId linked{ 0x00000000000000c1ull };

    const EntityId portal = scene.CreateBrush(Vec3d{ 0, 0, 0 });
    scene.GetRegistry().Components.AddComponent(portal, PortalComponent{ linked });

    const EntitySnapshot snapshot = document.CaptureEntity(portal);
    scene.DestroyEntity(portal);
    const EntityId restored = document.RestoreEntity(snapshot);

    ASSERT_TRUE(scene.IsPortal(restored));
    EXPECT_EQ(scene.TryGetPortal(restored)->Transition, linked);
}

// The scene loader fails the whole scene on any component-load failure, so the
// codec maps malformed text to the unlinked state (logged, then reported by
// validation) instead of failing: a broken link must not make a zone
// unloadable. Deviation from the spec's "fails the component load" recorded in
// the T1 commit.
TEST_F(PortalTest, TransitionIdCodecMapsMalformedToUnlinked)
{
    EditorDocument document(Logging);
    EditorScene& scene = document.GetScene();
    const TransitionId linked{ 0xabcdef0123456789ull };
    const EntityId portal = scene.CreateBrush(Vec3d{ 5, 0, 0 });
    scene.GetRegistry().Components.AddComponent(portal, PortalComponent{ linked });

    std::string text = JsonStringify(document.ToJson());
    const std::string encoded = TransitionIdToString(linked);
    const auto at = text.find(encoded);
    ASSERT_NE(at, std::string::npos);
    text.replace(at, encoded.size(), "zzzzzzzzzzzzzzzz");

    const auto json = JsonParse(text);
    ASSERT_TRUE(json.has_value());
    EditorDocument restored(Logging);
    ASSERT_TRUE(restored.LoadFromJson(*json));

    // The brush entity loads intact; the portal survives as unlinked, which
    // validation reports as partition.portal.unlinked.
    ASSERT_EQ(restored.GetScene().GetEntityCount(), 1u);
    const EntityId entity = restored.GetScene().GetAllEntities()[0];
    ASSERT_TRUE(restored.GetScene().IsPortal(entity));
    EXPECT_FALSE(restored.GetScene().TryGetPortal(entity)->Transition.IsValid());
    EXPECT_NE(restored.GetScene().TryGetTransform(entity), nullptr);
    EXPECT_NE(restored.GetScene().TryGetBrushMesh(entity), nullptr);
}

TEST_F(PortalTest, IsPortalReflectsComponentPresence)
{
    EditorDocument document(Logging);
    EditorScene& scene = document.GetScene();

    const EntityId plain = scene.CreateBrush(Vec3d{ 0, 0, 0 });
    const EntityId portal = scene.CreateBrush(Vec3d{ 4, 0, 0 });
    scene.GetRegistry().Components.AddComponent(portal, PortalComponent{});

    EXPECT_FALSE(scene.IsPortal(plain));
    EXPECT_TRUE(scene.IsPortal(portal));
    EXPECT_EQ(scene.TryGetPortal(plain), nullptr);
    EXPECT_NE(scene.TryGetPortal(portal), nullptr);
}

namespace
{

// Hub [-8..8] with Hallway [9..20] beyond +X and Attic straight above: the
// 3-zone shape the guess is tuned for.
WorldPartitionManifest GuessFixture()
{
    WorldPartitionManifest manifest;
    ZoneHeader hub;
    hub.Id = ZoneId{ 0xa1 };
    hub.Name = "Hub";
    hub.Bounds = Aabb3d::FromMinMax(Vec3d{ -8, 0, -8 }, Vec3d{ 8, 4, 8 });
    ZoneHeader hallway;
    hallway.Id = ZoneId{ 0xa2 };
    hallway.Name = "Hallway";
    hallway.Bounds = Aabb3d::FromMinMax(Vec3d{ 9, 0, -2 }, Vec3d{ 20, 4, 2 });
    ZoneHeader attic;
    attic.Id = ZoneId{ 0xa3 };
    attic.Name = "Attic";
    attic.Bounds = Aabb3d::FromMinMax(Vec3d{ -8, 10, -8 }, Vec3d{ 8, 14, 8 });
    manifest.Zones = { hub, hallway, attic };
    return manifest;
}

} // namespace

TEST(GuessPortalTargetZoneTest, PicksZoneAlongThinAxis)
{
    // A portal in Hub's +X wall, thin in X: the hallway lies along that axis.
    const Aabb3d portal =
        Aabb3d::FromCenterHalfExtent(Vec3d{ 8, 2, 0 }, Vec3d{ 0.2, 1.5, 1.5 });
    EXPECT_EQ(GuessPortalTargetZone(GuessFixture(), ZoneId{ 0xa1 }, portal), ZoneId{ 0xa2 });

    // Thin in Y (a floor hatch): the attic sits straight above.
    const Aabb3d hatch =
        Aabb3d::FromCenterHalfExtent(Vec3d{ 0, 4, 0 }, Vec3d{ 1.5, 0.2, 1.5 });
    EXPECT_EQ(GuessPortalTargetZone(GuessFixture(), ZoneId{ 0xa1 }, hatch), ZoneId{ 0xa3 });
}

TEST(GuessPortalTargetZoneTest, IgnoresOwnerAndOffAxisZones)
{
    // Thin in Z: no zone lies dominantly along Z, so there is no candidate.
    const Aabb3d portal =
        Aabb3d::FromCenterHalfExtent(Vec3d{ 8, 2, 0 }, Vec3d{ 1.5, 1.5, 0.2 });
    EXPECT_FALSE(GuessPortalTargetZone(GuessFixture(), ZoneId{ 0xa1 }, portal).IsValid());

    // From the hallway's perspective the same X-thin portal guesses back to
    // the NEAREST x-dominant zone, never the owner itself.
    const Aabb3d doorway =
        Aabb3d::FromCenterHalfExtent(Vec3d{ 9, 2, 0 }, Vec3d{ 0.2, 1.5, 1.5 });
    EXPECT_EQ(GuessPortalTargetZone(GuessFixture(), ZoneId{ 0xa2 }, doorway), ZoneId{ 0xa1 });
}

TEST(FitPortalBoxToFaceTest, ThinAxisFollowsFaceNormal)
{
    // A 4 x 3 wall face in the XY plane, normal +Z.
    const Vec3d xyFace[] = { { 0, 0, 5 }, { 4, 0, 5 }, { 4, 3, 5 }, { 0, 3, 5 } };
    PortalBoxFit fit = FitPortalBoxToFace(xyFace, Vec3d{ 0, 0, 1 }, 0.25);
    EXPECT_FLOAT_EQ(fit.Center[0], 2.0f);
    EXPECT_FLOAT_EQ(fit.Center[1], 1.5f);
    EXPECT_FLOAT_EQ(fit.Center[2], 5.0f);
    EXPECT_FLOAT_EQ(fit.HalfExtents[0], 2.0f);
    EXPECT_FLOAT_EQ(fit.HalfExtents[1], 1.5f);
    EXPECT_FLOAT_EQ(fit.HalfExtents[2], 0.125f);

    // Same footprint standing in YZ, normal -X.
    const Vec3d yzFace[] = { { 7, 0, 0 }, { 7, 0, 4 }, { 7, 3, 4 }, { 7, 3, 0 } };
    fit = FitPortalBoxToFace(yzFace, Vec3d{ -1, 0, 0 }, 0.25);
    EXPECT_FLOAT_EQ(fit.HalfExtents[0], 0.125f);
    EXPECT_FLOAT_EQ(fit.HalfExtents[1], 1.5f);
    EXPECT_FLOAT_EQ(fit.HalfExtents[2], 2.0f);

    // A floor face in XZ, normal +Y; a slightly tilted normal still snaps to
    // its dominant axis.
    const Vec3d floorFace[] = { { 0, 2, 0 }, { 4, 2, 0 }, { 4, 2, 4 }, { 0, 2, 4 } };
    fit = FitPortalBoxToFace(floorFace, Vec3d{ 0.1f, 0.9f, 0.1f }, 0.5);
    EXPECT_FLOAT_EQ(fit.HalfExtents[1], 0.25f);
    EXPECT_FLOAT_EQ(fit.HalfExtents[0], 2.0f);
}

TEST_F(PortalTest, CreatePortalBrushCommandIsOneUndoStep)
{
    EditorDocument document(Logging);
    EditorScene& scene = document.GetScene();

    auto create = MakeCreatePortalBrushCommand(Vec3d{ 3, 1, 0 }, Vec3d{ 2.0, 2.0, 0.125 },
                                               scene, document);
    CreateEntityCommand* command = create.get();
    command->Execute();
    const EntityId portal = command->GetCreatedEntity();

    ASSERT_TRUE(scene.HasEntity(portal));
    EXPECT_TRUE(scene.IsPortal(portal));
    EXPECT_FALSE(scene.TryGetPortal(portal)->Transition.IsValid());
    EXPECT_NE(scene.TryGetBrushMesh(portal), nullptr);
    const auto bounds = scene.TryGetWorldBounds(portal);
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(DominantPortalAxis(*bounds), 2);

    command->Undo();
    EXPECT_FALSE(scene.HasEntity(portal));
    EXPECT_EQ(scene.GetEntityCount(), 0u);
}

TEST(DerivePortalCounterpartTest, LargestStraddledOverlapWins)
{
    const WorldPartitionManifest manifest = GuessFixture();
    // A portal in Hub's +X wall reaches into the hallway when stretched along
    // its thin axis.
    const Aabb3d doorway =
        Aabb3d::FromCenterHalfExtent(Vec3d{ 8.5, 2, 0 }, Vec3d{ 0.2, 1.5, 1.5 });
    EXPECT_EQ(DerivePortalCounterpart(manifest, ZoneId{ 0xa1 }, doorway), ZoneId{ 0xa2 });

    // Deep inside Hub, touching nothing: the center-direction fallback still
    // points somewhere sane (the hallway lies along the thin axis).
    const Aabb3d interior =
        Aabb3d::FromCenterHalfExtent(Vec3d{ 0, 2, 0 }, Vec3d{ 0.2, 1.5, 1.5 });
    EXPECT_EQ(DerivePortalCounterpart(manifest, ZoneId{ 0xa1 }, interior), ZoneId{ 0xa2 });
}
