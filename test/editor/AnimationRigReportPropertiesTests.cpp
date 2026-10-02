#include "EditorDocumentsFixture.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigReportProperties.h"

#include "AnimationExampleContent.h"

#include <gtest/gtest.h>

// What a game's anim.risk would say of a rig, and what its previewed entity
// carries, read on the stage card's details.
TEST(AnimationRigReportProperties, TheReportMeasuresTheRigAndTheEntity)
{
    AnimationExampleContent content;
    EditorDocuments documents(content.Assets);
    AnimationPreviewWorkspace workspace(content.Assets, documents.Sources, documents.Store);
    AnimationRigReportProperties report(workspace);
    EXPECT_TRUE(report.Nodes().empty()) << "no rig, nothing to measure";

    ASSERT_TRUE(workspace.OpenRig("asset://animation/examples/moving.rig.sdata")) << workspace.Rig.Error;
    workspace.Rig.Simulation.RunTo(10);
    const std::vector<PropertyNode> nodes = report.Nodes();
    ASSERT_EQ(nodes.size(), 2u);
    EXPECT_EQ(nodes[0].Key, "risk");
    ASSERT_FALSE(nodes[0].Children.empty());
    EXPECT_NE(nodes[0].Children[0].Value.find("1 blend overrides"), std::string::npos) << nodes[0].Children[0].Value;
    EXPECT_EQ(nodes[1].Key, "entity");
    ASSERT_FALSE(nodes[1].Children.empty());
    EXPECT_NE(nodes[1].Children[0].Value.find("bytes"), std::string::npos);
    workspace.Rig.Simulation.Close();
}
