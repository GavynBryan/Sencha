// The Facts card over the fixture's hero rig: input facts driven as recorded
// scenario edits and cleared, derived facts described, and one fact plotted.

#include "properties/PropertyTree.h"
#include "EditorDocumentsFixture.h"
#include "authoring/AnimationFactProperties.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationTimelineLanes.h"

#include "AnimationExampleContent.h"

#include <anim/AnimFacts.h>

#include <gtest/gtest.h>

TEST(AnimationFactProperties, InputsAreDrivenDerivationsDescribedAndOneFactPlotted)
{
    AnimationExampleContent content;
    EditorDocuments documents(content.Assets);
    AnimationPreviewWorkspace workspace(content.Assets, documents.Sources, documents.Store);
    ASSERT_TRUE(workspace.OpenRig("asset://animation/hero.rig.sdata")) << workspace.Rig.Error;
    AnimationPreviewSession& session = workspace.Rig.Simulation;
    session.RunTo(20);
    AnimationFactProperties card(workspace);

    std::vector<PropertyNode> nodes = card.Nodes();
    ASSERT_NE(FindProperty(nodes, "facts.Speed"), nullptr);
    EXPECT_EQ(FindProperty(nodes, "facts.Speed")->Descriptor.Kind, PropertyKind::Number);
    ASSERT_NE(FindProperty(nodes, "facts.Grounded"), nullptr);
    EXPECT_EQ(FindProperty(nodes, "facts.Grounded")->Descriptor.Kind, PropertyKind::Toggle);
    ASSERT_NE(FindProperty(nodes, "facts.JustLanded"), nullptr);
    EXPECT_NE(FindProperty(nodes, "facts.JustLanded")->Value.find("edge("), std::string::npos)
        << FindProperty(nodes, "facts.JustLanded")->Value;
    EXPECT_FALSE(card.BeginEdit("facts.JustLanded")) << "a derived fact is not driven";

    ASSERT_TRUE(card.BeginEdit("facts.Speed"));
    EXPECT_EQ(card.CommitEdit("facts.Speed", "2.5"), PropertyEditResult::Applied);
    session.Step();
    ASSERT_NE(session.Input("Speed"), nullptr) << "applied on the next tick";
    const int speed = session.Rig()->FindSlot("Speed");
    EXPECT_FLOAT_EQ(AnimFactToFloat(session.Facts()[static_cast<std::size_t>(speed)]), 2.5f);
    EXPECT_EQ(card.CommitEdit("facts.Speed", "fast"), PropertyEditResult::Refused);
    EXPECT_FALSE(workspace.Sources.CanUndo()) << "a scenario edit, not a document's";

    nodes = card.Nodes();
    const auto clear = std::ranges::find(FindProperty(nodes, "facts.Speed")->Descriptor.Verbs, "clear", &PropertyVerb::Id);
    ASSERT_NE(clear, FindProperty(nodes, "facts.Speed")->Descriptor.Verbs.end());
    EXPECT_EQ(card.Act("facts.Speed", "clear", ""), PropertyEditResult::Applied);
    session.Step();
    EXPECT_EQ(session.Input("Speed"), nullptr);

    EXPECT_TRUE(card.Plotted().empty());
    EXPECT_EQ(card.Act("facts.Speed", "plot", ""), PropertyEditResult::Applied);
    EXPECT_EQ(card.Plotted(), "Speed");
    const TimelineLane lane = AnimationFactLane(session, card.Plotted());
    EXPECT_EQ(lane.Samples.size(), session.History().size());
    EXPECT_DOUBLE_EQ(lane.Samples.back().Value, 2.5);
    EXPECT_EQ(card.Act("facts.Speed", "plot", ""), PropertyEditResult::Applied);
    EXPECT_TRUE(card.Plotted().empty()) << "plotting it again puts it away";
    EXPECT_TRUE(AnimationFactLane(session, "Nothing").Samples.empty());
    session.Close();
}
