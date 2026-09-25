// Scenes that still name the retired clip player become one-layer rigs that
// play the clip as the player did, and the scenes carry the rigs instead.

#include "authoring/AnimationClipPlayerMigration.h"
#include "authoring/AnimationPreviewWorkspace.h"

#include "AnimationTestProject.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    constexpr const char* kSkeleton = "asset://meshes/man.blend#skel:Man";
    constexpr const char* kClip = "asset://meshes/man.blend#anim:Wave";

    constexpr std::string_view kScene = R"({
  "format_version": 1,
  "entities": [
    { "id": "01", "components": { "AnimationClipPlayer": { "clip": "asset://meshes/man.blend#anim:Wave",
                                                           "time_seconds": 0.5, "rate": 0.0, "loop": true } } },
    { "id": "02", "components": { "AnimationClipPlayer": { "clip": "asset://meshes/man.blend#anim:Wave",
                                                           "time_seconds": 0.5, "rate": 0.0, "loop": true } } },
    { "id": "03", "components": { "AnimationClipPlayer": { "clip": "asset://meshes/man.blend#anim:Wave",
                                                           "rate": -1.0, "loop": false },
                                  "Transform": { "local": { "position": [ 1, 2, 3 ] } } } },
    { "id": "04", "components": { "Transform": { "local": { "position": [ 0, 0, 0 ] } } } }
  ]
})";

    struct Project : AnimationTestProject
    {
        Project() : AnimationTestProject("sencha_clip_player_migration")
        {
            Write("levels/old.sscene", kScene);
            Skeleton(kSkeleton);
            Clip(kClip, kSkeleton);
        }
    };
}

TEST(AnimationClipPlayerMigration, TheReportNamesEveryPlayer)
{
    Project project;
    std::vector<std::string> problems;
    const std::vector<AnimationClipPlayerUse> uses = FindAnimationClipPlayers(project.Root, problems);
    EXPECT_TRUE(problems.empty());
    ASSERT_EQ(uses.size(), 3u);
    EXPECT_EQ(uses[0].Scene, "levels/old.sscene");
    EXPECT_EQ(uses[0].Entity, "01");
    EXPECT_DOUBLE_EQ(uses[0].TimeSeconds, 0.5);
    EXPECT_DOUBLE_EQ(uses[0].Rate, 0.0);
    EXPECT_FALSE(uses[2].Loop);
}

// Two players with the same clip and settings share a rig; a third playing
// it another way gets its own. Each rig plays as its player did.
TEST(AnimationClipPlayerMigration, PlayersBecomeRigsThatPlayAsTheyDid)
{
    Project project;
    AnimationPreviewWorkspace workspace(*project.Assets, {}, project.Root);
    std::string error;
    ASSERT_TRUE(workspace.MigrateClipPlayers(error)) << error;
    EXPECT_TRUE(workspace.ClipPlayerUses.empty()) << "nothing left to migrate";

    const JsonValue scene = project.Read("levels/old.sscene");
    const JsonValue::Array& entities = scene.Find("entities")->AsArray();
    const auto rigOf = [&](std::size_t index) {
        const JsonValue* components = entities[index].Find("components");
        EXPECT_EQ(components->Find("AnimationClipPlayer"), nullptr);
        const JsonValue* rig = components->Find("anim_rig");
        return rig != nullptr ? rig->Find("rig")->AsString() : std::string();
    };
    EXPECT_EQ(rigOf(0), "asset://animation/migrated/Wave.rig.sdata");
    EXPECT_EQ(rigOf(1), rigOf(0));
    EXPECT_EQ(rigOf(2), "asset://animation/migrated/Wave_2.rig.sdata");
    EXPECT_NE(entities[2].Find("components")->Find("Transform"), nullptr) << "what else it carried stays";
    EXPECT_EQ(entities[3].Find("components")->Find("anim_rig"), nullptr);
    EXPECT_TRUE(std::filesystem::exists(project.Root / "animation/migrated/migrated.tags.sdata"));

    // Held at 0.5s, as the paused player was.
    ASSERT_TRUE(workspace.OpenRig("asset://animation/migrated/Wave.rig.sdata")) << workspace.ScenarioError;
    ASSERT_TRUE(workspace.Simulation.Rig()->Valid);
    workspace.Simulation.RunTo(30);
    EXPECT_FLOAT_EQ(workspace.Simulation.Content()->Layers[0].TimeSeconds, 0.5f);
    // Backwards from the start, clamped: it rests at the start.
    ASSERT_TRUE(workspace.OpenRig("asset://animation/migrated/Wave_2.rig.sdata")) << workspace.ScenarioError;
    workspace.Simulation.RunTo(30);
    EXPECT_FLOAT_EQ(workspace.Simulation.Content()->Layers[0].TimeSeconds, 0.0f);
    workspace.Simulation.Close();

    // Converting again finds nothing and writes nothing.
    EXPECT_TRUE(workspace.MigrateClipPlayers(error)) << error;
}
