// The Flow lens over the pump reload example: the card says where the flow is
// and how each section is left, the deck's lane runs its sections, and the
// flow's sheet keeps control going forward.

#include "properties/PropertyTree.h"
#include "EditorDocumentsFixture.h"
#include "authoring/AnimationFlowProperties.h"
#include "authoring/AnimationPredicateAuthoring.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationTimelineLanes.h"
#include "data/DataProperties.h"

#include "AnimationExampleContent.h"

#include <anim/AnimFlowData.h>

#include <gtest/gtest.h>

namespace
{
constexpr const char* kRig = "asset://animation/examples/pump_reload.rig.sdata";
constexpr const char* kFlow = "asset://animation/examples/pump_reload.flow.sdata";

struct FlowLens : testing::Test
{
    AnimationExampleContent Content;
    std::unique_ptr<EditorDocuments> Documents;
    std::unique_ptr<AnimationPreviewWorkspace> Workspace;

    void SetUp() override
    {
        Documents = std::make_unique<EditorDocuments>(Content.Assets);
        Workspace = std::make_unique<AnimationPreviewWorkspace>(Content.Assets, Documents->Sources, Documents->Store);
    }

    void TearDown() override
    {
        Workspace->Sources.DiscardAll();
        Workspace->Rig.Simulation.Close();
        Workspace.reset();
        Documents.reset();
    }
};

} // namespace

TEST_F(FlowLens, TheCardAndTheLaneFollowTheFlowALayerPlays)
{
    ASSERT_TRUE(Workspace->OpenRig(kRig)) << Workspace->Rig.Error;
    Workspace->Rig.Simulation.RunTo(120);
    std::vector<std::string> opened;
    AnimationFlowProperties card(*Workspace, [&](const std::string& path) { opened.push_back(path); });

    const std::vector<PropertyNode> nodes = card.Nodes();
    ASSERT_NE(FindProperty(nodes, "flow"), nullptr) << "the reload issued at tick 10 plays a flow";
    EXPECT_EQ(FindProperty(nodes, "flow")->Value, kFlow);
    EXPECT_EQ(FindProperty(nodes, "cancel")->Value, "goes to Anim.Reload.Exit");
    ASSERT_NE(FindProperty(nodes, "sections.1"), nullptr);
    EXPECT_NE(FindProperty(nodes, "sections.1")->Value.find("repeats while"), std::string::npos);
    EXPECT_NE(FindProperty(nodes, "sections.1")->Value.find("a cancel leaves at once"), std::string::npos);
    EXPECT_EQ(card.Act("flow", "open", ""), PropertyEditResult::Applied);
    EXPECT_EQ(opened, std::vector<std::string>{ kFlow });

    const TimelineLane lane = AnimationFlowLane(Workspace->Rig.Simulation, 0);
    ASSERT_GE(lane.Spans.size(), 2u);
    EXPECT_EQ(lane.Spans[0].Label, "Anim.Reload.Enter");
    EXPECT_EQ(lane.Spans[1].Label.rfind("Anim.Reload.Insert", 0), 0u);
    const std::optional<AnimationLanePick> pick = ParseAnimationLaneKey(lane.Spans[1].Key);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->Names, AnimationLanePick::What::Section);
    EXPECT_EQ(pick->Index, 1);
}

TEST_F(FlowLens, TheSheetKeepsControlGoingForward)
{
    DataDocument* flow = Workspace->Documents.OpenOrFocus(kFlow, Workspace->DocumentError);
    ASSERT_NE(flow, nullptr) << Workspace->DocumentError;
    DataSubtypeAuthoringRegistry authorings;
    RegisterAnimationSubtypeAuthoring(authorings);
    IDataSubtypeAuthoring* authoring = authorings.Find(kAnimFlowType);
    ASSERT_NE(authoring, nullptr);
    authoring->Update(*flow, Workspace->Documents);
    DataProperties sheet(Workspace->Documents, [&] { return flow; }, &authorings);
    const auto sections = [&] { return flow->Data()->Find("sections")->AsArray(); };

    std::vector<PropertyNode> nodes = sheet.Nodes();
    ASSERT_NE(FindProperty(nodes, "$.data.cancel"), nullptr);
    EXPECT_EQ(FindProperty(nodes, "$.data.cancel")->Descriptor.Choices,
              (std::vector<std::string>{ "Anim.Reload.Enter", "Anim.Reload.Insert", "Anim.Reload.Exit" }));
    ASSERT_NE(FindProperty(nodes, "$.data.sections[1].while[0]"), nullptr);
    EXPECT_EQ(FindProperty(nodes, "$.data.sections[1].while[0]")->Descriptor.Label, "not AmmoFull");

    // A branch from the first section names only a later one.
    EXPECT_EQ(sheet.Act("$.data.sections[0].branches", "add", ""), PropertyEditResult::Applied);
    nodes = sheet.Nodes();
    ASSERT_NE(FindProperty(nodes, "$.data.sections[0].branches[0].to"), nullptr);
    EXPECT_EQ(FindProperty(nodes, "$.data.sections[0].branches[0].to")->Descriptor.Choices,
              (std::vector<std::string>{ "", "Anim.Reload.Insert", "Anim.Reload.Exit" }));
    ASSERT_TRUE(sheet.BeginEdit("$.data.sections[0].branches[0].to"));
    EXPECT_EQ(sheet.CommitEdit("$.data.sections[0].branches[0].to", "Anim.Reload.Exit"), PropertyEditResult::Applied);

    // Exit may rise past Insert, but not past the section that branches to it.
    EXPECT_EQ(sheet.Act("$.data.sections[2]", "up", ""), PropertyEditResult::Applied);
    EXPECT_EQ(sections()[1].Find("tag")->AsString(), "Anim.Reload.Exit");
    EXPECT_EQ(sheet.Act("$.data.sections[1]", "up", ""), PropertyEditResult::Refused);
    EXPECT_EQ(sections()[0].Find("tag")->AsString(), "Anim.Reload.Enter");

    // A loop's kind drops what another kind needed.
    ASSERT_TRUE(sheet.BeginEdit("$.data.sections[2].loop"));
    EXPECT_EQ(sheet.CommitEdit("$.data.sections[2].loop", "Once"), PropertyEditResult::Applied);
    EXPECT_EQ(sections()[2].Find("while"), nullptr);

    // Removing the cancel section takes the branch to it and the cancel along.
    EXPECT_EQ(sheet.Act("$.data.sections[1]", "delete", ""), PropertyEditResult::Applied);
    EXPECT_EQ(sections().size(), 2u);
    EXPECT_EQ(flow->Data()->Find("cancel"), nullptr);
    EXPECT_TRUE(sections()[0].Find("branches")->AsArray().empty());
}
