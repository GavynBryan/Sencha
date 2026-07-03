#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

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
