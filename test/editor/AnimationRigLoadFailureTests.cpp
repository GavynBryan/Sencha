#include "EditorDocumentsFixture.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationStageProperties.h"
#include "problems/AssetLoadProblems.h"
#include "problems/ProblemList.h"

#include "AnimationExampleContent.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

// A rig that does not load says why, where the person asked for it, and the
// asset at fault is a problem of its own.
namespace
{
class BrokenRigRoot
{
public:
    BrokenRigRoot()
    {
        std::random_device seed;
        Root = std::filesystem::temp_directory_path() / ("sencha_broken_rig_" + std::to_string(seed()));
        std::filesystem::create_directories(Root / "animation/broken");
        Write("animation/broken/broken.rig.sdata", R"({ "type": "animation.rig", "version": 1, "data": {
            "skeleton": "asset://meshes/dev/golden_rig.sskel",
            "facts": "asset://animation/examples/examples.facts.sdata",
            "behaviors": [ "asset://animation/examples/moving.behaviors.sdata" ],
            "slot_maps": [ "asset://animation/broken/broken.slots.sdata" ],
            "layers": [ { "name": "anim.layer.base", "selector": "asset://animation/examples/moving.selector.sdata",
                          "idle": "Anim.Locomotion.Idle" } ] } })");
        Write("animation/broken/broken.slots.sdata", R"({ "type": "animation.slot_map", "version": 1, "data": {
            "rows": [ { "behavior": "Anim.Locomotion.Idle", "clip": "asset://meshes/dev/golden_rig.sanim" } ] } })");
    }
    ~BrokenRigRoot()
    {
        std::error_code ignored;
        std::filesystem::remove_all(Root, ignored);
    }

    std::filesystem::path Root;

private:
    void Write(const char* relative, const char* text) const { std::ofstream(Root / relative) << text; }
};
} // namespace

TEST(AnimationRigLoadFailure, TheStageCardAndTheProblemsSayWhyARigDidNotLoad)
{
    AnimationExampleContent content;
    BrokenRigRoot broken;
    ASSERT_TRUE(ScanAssetsDirectory(broken.Root.generic_string(), content.Assets.Registry, content.Assets.Assets.Kinds()));
    EditorDocuments documents(content.Assets);
    AnimationPreviewWorkspace workspace(content.Assets, documents.Sources, documents.Store);

    EXPECT_FALSE(workspace.OpenRig("asset://animation/broken/broken.rig.sdata"));
    EXPECT_NE(workspace.Rig.Error.find("broken.rig.sdata did not load"), std::string::npos) << workspace.Rig.Error;
    EXPECT_NE(workspace.Rig.Error.find("broken.slots.sdata"), std::string::npos) << workspace.Rig.Error;

    const std::string first = workspace.Rig.Error;
    EXPECT_FALSE(workspace.OpenRig("asset://animation/broken/broken.rig.sdata"));
    EXPECT_EQ(workspace.Rig.Error, first) << "asking again gives the same reason";

    const std::vector<PropertyNode> card = AnimationScenarioProperties(workspace).Nodes();
    ASSERT_EQ(card.size(), 1u) << "with no rig open, the card still carries the refusal";
    EXPECT_EQ(card[0].Value, workspace.Rig.Error);

    ProblemList problems;
    AssetLoadProblems publisher(problems);
    publisher.Update(content.Assets.Assets);
    bool slotsNamed = false;
    for (const Problem* problem : problems.All())
        if (problem->Location == "asset://animation/broken/broken.slots.sdata")
        {
            slotsNamed = true;
            EXPECT_NE(problem->Message.find("id"), std::string::npos) << problem->Message;
        }
    EXPECT_TRUE(slotsNamed) << "the slot map at fault is a problem of its own";
}
