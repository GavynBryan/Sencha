#include "EditorDocumentsFixture.h"
#include "authoring/AnimationLayerProperties.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigRecipe.h"

#include "AnimationTestProject.h"

#include <gtest/gtest.h>

// The Layers card: the rig's layers as they composed on the inspected tick,
// muted and soloed on the stage alone, and a layer's mask taken apart step by
// step on the rig document.
namespace
{
constexpr const char* kSkeleton = "asset://meshes/man.blend#skel:Man";
constexpr const char* kRig = "asset://animation/layered/layered.rig.sdata";

const PropertyNode* NodeAt(const std::vector<PropertyNode>& nodes, std::string_view key)
{
    for (const PropertyNode& node : nodes)
    {
        if (node.Key == key)
            return &node;
        if (const PropertyNode* child = NodeAt(node.Children, key))
            return child;
    }
    return nullptr;
}
} // namespace

TEST(AnimationLayerProperties, LayersAreListedInspectedMutedAndUnmasked)
{
    AnimationTestProject project("sencha_layer_properties");
    project.Skeleton(kSkeleton, { { "root", -1 }, { "spine", 0 }, { "arm", 1 } });
    const std::vector<std::string> clips = { "asset://meshes/man.blend#anim:Idle", "asset://meshes/man.blend#anim:Walk",
                                             "asset://meshes/man.blend#anim:Left_claw" };
    for (const std::string& clip : clips)
        project.Clip(clip.c_str(), kSkeleton);
    project.ScanEngineAssets();
    EditorDocuments documents(*project.Assets);
    AnimationPreviewWorkspace workspace(*project.Assets, documents.Sources, documents.Store, {}, project.Root);
    std::string error;
    ASSERT_TRUE(workspace.CreateRig(
        { .Name = "layered", .Clips = clips, .Preset = AnimationRigPreset::Character, .UpperBodyJoint = "spine" },
        error))
        << error;
    ASSERT_TRUE(workspace.OpenRig(kRig)) << workspace.Rig.Error;
    ASSERT_GE(workspace.Rig.Simulation.Rig()->Layers.size(), 2u);
    workspace.Rig.Simulation.Step();

    AnimationLayerProperties card(workspace);
    EXPECT_EQ(card.Title(), "Layers");
    std::vector<PropertyNode> nodes = card.Nodes();
    ASSERT_NE(NodeAt(nodes, "layers.0"), nullptr);
    ASSERT_NE(NodeAt(nodes, "layers.1"), nullptr);
    EXPECT_NE(NodeAt(nodes, "layers.1")->Value.find("of 3 joints"), std::string::npos) << NodeAt(nodes, "layers.1")->Value;

    // Muting changes the stage alone.
    EXPECT_EQ(card.Act("layers.1", "mute", ""), PropertyEditResult::Applied);
    EXPECT_FALSE(workspace.Viewport.LayerDisplay.Shows(1));
    EXPECT_TRUE(workspace.Viewport.LayerDisplay.Shows(0));
    EXPECT_EQ(card.Act("layers.0", "solo", ""), PropertyEditResult::Applied);
    EXPECT_FALSE(workspace.Viewport.LayerDisplay.Shows(1));
    EXPECT_FALSE(workspace.Sources.CanUndo()) << "nothing authored changed";

    EXPECT_EQ(card.Act("layers.1", "inspect", ""), PropertyEditResult::Applied);
    EXPECT_EQ(workspace.Rig.Navigation.Layer, 1u);
    EXPECT_EQ(card.Title(), workspace.Rig.Simulation.Rig()->Layers[1].NameText);
    nodes = card.Nodes();
    ASSERT_NE(NodeAt(nodes, "muted"), nullptr);
    EXPECT_EQ(NodeAt(nodes, "muted")->Value, "true");
    ASSERT_TRUE(card.BeginEdit("muted"));
    EXPECT_EQ(card.CommitEdit("muted", "false"), PropertyEditResult::Applied);
    EXPECT_EQ(workspace.Viewport.LayerDisplay.Muted, 0);
    ASSERT_NE(NodeAt(nodes, "mask.0"), nullptr) << "the upper body is masked from the spine";
    EXPECT_EQ(NodeAt(nodes, "mask.0")->Value, "spine and below");

    EXPECT_EQ(card.Act("mask.0", "remove", ""), PropertyEditResult::Applied);
    nodes = card.Nodes();
    EXPECT_NE(NodeAt(nodes, "mask.none"), nullptr);
    EXPECT_FALSE(workspace.Rig.Simulation.Rig()->Layers[1].Masked()) << "the preview follows the edit";
    workspace.Sources.Undo();
    EXPECT_TRUE(workspace.Rig.Simulation.Rig()->Layers[1].Masked());

    EXPECT_TRUE(card.StepBack());
    EXPECT_EQ(card.Title(), "Layers");
    EXPECT_FALSE(card.StepBack());
    workspace.Sources.DiscardAll();
    workspace.Rig.Simulation.Close();
}
