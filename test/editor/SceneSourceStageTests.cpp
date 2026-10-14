#include "PrefabSourceFixture.h"

#include "document/DocumentSerialization.h"
#include "document/placement/PlacementSession.h"
#include "render/PlacementGhost.h"
#include "render/SceneSourceStage.h"

#include <cstring>
#include <map>
#include <numbers>
#include <string>
#include <vector>

// The ghost of a prefab is the prefab placed for real in a scratch document,
// so what it shows is what a placement makes; the ghost keeps those stages
// prepared while the prefab is in hand and prepares afresh only when one of
// its sources changed.
namespace
{
class SceneSourceStageTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    // Every named entity's world transform, by name.
    [[nodiscard]] static std::map<std::string, Transform3f> WorldsByName(const EditorScene& scene)
    {
        std::map<std::string, Transform3f> worlds;
        for (const EntityId entity : scene.GetAllEntities())
        {
            const auto* name = scene.GetRegistry().Components.TryGet<EntityNameComponent>(entity);
            const Transform3f* world = scene.TryGetWorldTransform(entity);
            if (name != nullptr && world != nullptr && !name->Value.View().empty())
                worlds.emplace(std::string(name->Value.View()), *world);
        }
        return worlds;
    }

    [[nodiscard]] static Transform3f Turned(Vec3d at, float yaw)
    {
        return PlacementTransform(UprightSite(at), yaw);
    }

    LoggingProvider Logging;
    PrefabSourceFixture Prefabs{ Logging };
};

[[nodiscard]] bool SameBits(const Transform3f& a, const Transform3f& b)
{
    return std::memcmp(&a, &b, sizeof(Transform3f)) == 0;
}
} // namespace

TEST_F(SceneSourceStageTest, AStagedPrefabStandsExactlyWhereThePlacedOneDoes)
{
    for (const std::string& source : { Prefabs.Door, Prefabs.Crate })
    {
        const Transform3f placement = Turned(Vec3d(3.25f, 0.5f, -7.75f), 2.3f);

        EditorDocument level(Logging);
        level.SetContentRoots({ Prefabs.Root });
        ASSERT_TRUE(level.PlaceSceneInstance(source, placement).IsValid());
        level.GetScene().RefreshDerivedTransforms();

        std::string error;
        const std::unique_ptr<SceneSourceStage> stage =
            SceneSourceStage::Prepare(source, { Prefabs.Root }, nullptr, Logging, &error);
        ASSERT_NE(stage, nullptr) << error;
        stage->Pose(placement);
        stage->Staged().Build();

        const std::map<std::string, Transform3f> placed = WorldsByName(level.GetScene());
        const std::map<std::string, Transform3f> staged = WorldsByName(stage->Staged().Scene());
        ASSERT_FALSE(placed.empty());
        ASSERT_EQ(placed.size(), staged.size()) << source;
        for (const auto& [name, world] : placed)
        {
            ASSERT_TRUE(staged.contains(name)) << source << ": " << name;
            EXPECT_TRUE(SameBits(staged.at(name), world)) << source << ": " << name << " stands elsewhere";
        }
    }
}

TEST_F(SceneSourceStageTest, ANestedPrefabIsStagedWithEverythingItPlaces)
{
    const std::unique_ptr<SceneSourceStage> stage =
        SceneSourceStage::Prepare(Prefabs.Crate, { Prefabs.Root }, nullptr, Logging);
    ASSERT_NE(stage, nullptr);
    stage->Staged().Build();
    const std::map<std::string, Transform3f> staged = WorldsByName(stage->Staged().Scene());
    EXPECT_TRUE(staged.contains("Lid"));
    EXPECT_TRUE(staged.contains("Body")) << "the door the crate places";
    EXPECT_TRUE(staged.contains("Lamp")) << "the door's own child";
    EXPECT_TRUE(stage->DependsOnAny(std::vector<std::string>{ Prefabs.Door }));
}

TEST_F(SceneSourceStageTest, ItKnowsWhenASourceItPlacedChangedOnDisk)
{
    const std::unique_ptr<SceneSourceStage> stage =
        SceneSourceStage::Prepare(Prefabs.Crate, { Prefabs.Root }, nullptr, Logging);
    ASSERT_NE(stage, nullptr);
    EXPECT_FALSE(stage->ChangedOnDisk());
    Prefabs.WriteDoor(2.75f);
    EXPECT_TRUE(stage->ChangedOnDisk()) << "the nested door changed";
}

TEST_F(SceneSourceStageTest, ASourceThatDoesNotPlaceIsRefusedWithItsReason)
{
    std::string error;
    EXPECT_EQ(SceneSourceStage::Prepare("asset://props/nowhere.sscene", { Prefabs.Root }, nullptr, Logging, &error),
              nullptr);
    EXPECT_FALSE(error.empty());
}

TEST_F(SceneSourceStageTest, ADocumentIsNotPlacedInsideItself)
{
    EditorDocument level(Logging);
    level.SetContentRoots({ Prefabs.Root });
    ASSERT_TRUE(level.SaveAs((Prefabs.Root / "level.sscene").generic_string()));
    std::string error;
    EXPECT_FALSE(level.CanPlaceSceneSource("asset://level.sscene", &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(level.PlaceSceneInstance("asset://level.sscene", Transform3f::Identity()).IsValid());
    EXPECT_TRUE(level.CanPlaceSceneSource(Prefabs.Door));
}

namespace
{
class PlacementGhostTest : public SceneSourceStageTest
{
protected:
    void SetUp() override
    {
        Level.SetContentRoots({ Prefabs.Root });
        Ghost.SetContentRoots({ Prefabs.Root });
    }

    void Hold(const std::string& source, Vec3d at, float yaw = 0.0f)
    {
        Session.Aim({ .What = PlacementSubject::Kind::SceneSource, .Key = source }, UprightSite(at), yaw, Level);
        Ghost.Update(Session, Level.GetScene().InteractiveEvaluationPolicy(), 4);
    }

    EditorDocument Level{ Logging };
    PlacementSession Session;
    PlacementGhost Ghost{ nullptr, Logging };
};
} // namespace

TEST_F(PlacementGhostTest, ItShowsThePendingPrefabPosedAtThePendingTransform)
{
    Hold(Prefabs.Door, Vec3d(2.0f, 0.0f, 1.0f), 0.8f);
    const SceneSourceStage* shown = Ghost.Shown();
    ASSERT_NE(shown, nullptr);
    const Transform3f* root = shown->Staged().Scene().TryGetLocalTransform(shown->Root());
    ASSERT_NE(root, nullptr);
    EXPECT_TRUE(SameBits(*root, Session.Pending()->Placement));
}

TEST_F(PlacementGhostTest, MovingAndTurningPrepareNothing)
{
    Hold(Prefabs.Door, Vec3d(0.0f, 0.0f, 0.0f));
    const SceneSourceStage* first = Ghost.Shown();
    Hold(Prefabs.Door, Vec3d(5.0f, 0.0f, 5.0f), 1.0f);
    EXPECT_EQ(Ghost.Shown(), first);
}

TEST_F(PlacementGhostTest, APrefabBackInHandIsTheOnePreparedBefore)
{
    Hold(Prefabs.Door, Vec3d{});
    const SceneSourceStage* door = Ghost.Shown();
    Hold(Prefabs.Crate, Vec3d{});
    EXPECT_NE(Ghost.Shown(), door);
    Hold(Prefabs.Door, Vec3d{});
    EXPECT_EQ(Ghost.Shown(), door) << "switching back prepared it again";
}

TEST_F(PlacementGhostTest, ASavedSourcePreparesAfreshWhateverPlacesIt)
{
    Hold(Prefabs.Crate, Vec3d{});
    const SceneSourceStage* before = Ghost.Shown();
    const std::vector<std::string> saved{ Prefabs.Door };
    Ghost.Invalidate(saved);
    Hold(Prefabs.Crate, Vec3d{});
    EXPECT_NE(Ghost.Shown(), before) << "the crate places the saved door";
}

TEST_F(PlacementGhostTest, APrefabChangedOnDiskPreparesAfreshWhenItComesBackIntoHand)
{
    Hold(Prefabs.Door, Vec3d{});
    const SceneSourceStage* before = Ghost.Shown();
    Prefabs.WriteDoor(3.0f);
    Hold(Prefabs.Door, Vec3d(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(Ghost.Shown(), before) << "in hand, it only poses";
    Session.Clear();
    Ghost.Update(Session, Level.GetScene().InteractiveEvaluationPolicy(), 4);
    Hold(Prefabs.Door, Vec3d{});
    EXPECT_NE(Ghost.Shown(), before);
}

TEST_F(PlacementGhostTest, NothingIsShownForAnEntityOrAPrefabThatWillNotPlace)
{
    Session.Aim({ .What = PlacementSubject::Kind::Entity, .Key = "PointLight" }, UprightSite(Vec3d{}), 0.0f, Level);
    Ghost.Update(Session, Level.GetScene().InteractiveEvaluationPolicy(), 4);
    EXPECT_EQ(Ghost.Shown(), nullptr);

    Hold("asset://props/nowhere.sscene", Vec3d{});
    EXPECT_EQ(Ghost.Shown(), nullptr);
    EXPECT_FALSE(Ghost.Failure().empty());
}

TEST_F(PlacementGhostTest, PreviewingLeavesTheLevelUntouched)
{
    Hold(Prefabs.Crate, Vec3d(1.0f, 0.0f, 1.0f));
    ASSERT_NE(Ghost.Shown(), nullptr);
    EXPECT_FALSE(Level.IsDirty());
    EXPECT_TRUE(Level.GetScene().GetAllEntities().empty());
}
