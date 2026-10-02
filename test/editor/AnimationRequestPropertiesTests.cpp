// The Requests lens over the fixture's hero rig: a request drafted on the card
// and issued on the next tick, cancelled for a reason, laned along the deck,
// and the request schema's sheet counting parameters against their budget.

#include "EditorDocumentsFixture.h"
#include "authoring/AnimationPredicateAuthoring.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRequestProperties.h"
#include "authoring/AnimationTimelineLanes.h"
#include "data/DataProperties.h"

#include "AnimationExampleContent.h"

#include <anim/AnimRequestSchema.h>

#include <gtest/gtest.h>

namespace
{
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

const PropertyNode* LiveRequest(const std::vector<PropertyNode>& nodes, std::string_view label)
{
    const PropertyNode* live = NodeAt(nodes, "live");
    if (live == nullptr)
        return nullptr;
    for (const PropertyNode& row : live->Children)
        if (row.Descriptor.Label == label)
            return &row;
    return nullptr;
}
} // namespace

TEST(AnimationRequestProperties, ARequestIsDraftedIssuedCancelledAndLaned)
{
    AnimationExampleContent content;
    EditorDocuments documents(content.Assets);
    AnimationPreviewWorkspace workspace(content.Assets, documents.Sources, documents.Store);
    ASSERT_TRUE(workspace.OpenRig("asset://animation/hero.rig.sdata")) << workspace.Rig.Error;
    AnimationPreviewSession& session = workspace.Rig.Simulation;
    session.RunTo(5);
    AnimationRequestProperties card(workspace);

    std::vector<PropertyNode> nodes = card.Nodes();
    ASSERT_NE(NodeAt(nodes, "issue.source"), nullptr);
    ASSERT_NE(NodeAt(nodes, "issue.intent"), nullptr);
    EXPECT_EQ(NodeAt(nodes, "issue.intent")->Descriptor.Kind, PropertyKind::Choice);
    ASSERT_TRUE(card.BeginEdit("issue.intent"));
    EXPECT_EQ(card.CommitEdit("issue.intent", "Anim.Melee"), PropertyEditResult::Applied);
    nodes = card.Nodes();
    ASSERT_NE(NodeAt(nodes, "issue.params.0"), nullptr) << "the intent's parameter is drafted with it";
    EXPECT_EQ(NodeAt(nodes, "issue.params.0")->Descriptor.Label, "combo");
    EXPECT_EQ(card.CommitEdit("issue.params.0", "2"), PropertyEditResult::Applied);
    EXPECT_EQ(card.CommitEdit("issue.lifetime", "Fixed"), PropertyEditResult::Applied);
    nodes = card.Nodes();
    EXPECT_NE(NodeAt(nodes, "issue.ticks"), nullptr) << "a fixed request says how long";
    EXPECT_EQ(card.CommitEdit("issue.lifetime", "Held"), PropertyEditResult::Applied);

    EXPECT_EQ(card.Act("issue", "issue", ""), PropertyEditResult::Applied);
    session.Step();
    nodes = card.Nodes();
    const PropertyNode* melee = LiveRequest(nodes, "Anim.Melee from player");
    ASSERT_NE(melee, nullptr);
    EXPECT_NE(melee->Value.find("live"), std::string::npos) << melee->Value;
    ASSERT_EQ(melee->Descriptor.Verbs.size(), 1u);
    EXPECT_EQ(melee->Descriptor.Verbs[0].Choices.size(), 4u) << "a cancel says why";

    const std::string key = melee->Key;
    EXPECT_EQ(card.Act(key, "cancel", "Interrupted"), PropertyEditResult::Applied);
    session.Step();
    nodes = card.Nodes();
    melee = LiveRequest(nodes, "Anim.Melee from player");
    ASSERT_NE(melee, nullptr) << "kept while its tail plays out";
    EXPECT_NE(melee->Value.find("interrupted at"), std::string::npos) << melee->Value;
    EXPECT_EQ(card.Act(key, "cancel", "Because"), PropertyEditResult::Gone);

    const TimelineLane lane = AnimationRequestLane(session);
    ASSERT_FALSE(lane.Spans.empty());
    EXPECT_EQ(lane.Spans.back().Label, "Anim.Melee");
    const std::optional<AnimationLanePick> pick = ParseAnimationLaneKey(lane.Spans.back().Key);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->Names, AnimationLanePick::What::Request);
    EXPECT_FALSE(workspace.Sources.CanUndo()) << "issuing is a scenario edit";
    session.Close();
}

TEST(AnimationRequestProperties, TheSchemaSheetCountsParametersAgainstTheBudget)
{
    AnimationExampleContent content;
    EditorDocuments documents(content.Assets);
    AnimationPreviewWorkspace workspace(content.Assets, documents.Sources, documents.Store);
    DataDocument* schema = workspace.Documents.OpenOrFocus("asset://animation/requests.sdata", workspace.DocumentError);
    ASSERT_NE(schema, nullptr) << workspace.DocumentError;
    DataSubtypeAuthoringRegistry authorings;
    RegisterAnimationSubtypeAuthoring(authorings);
    ASSERT_NE(authorings.Find(kAnimRequestSchemaType), nullptr);
    DataProperties sheet(workspace.Documents, [&] { return schema; }, &authorings);

    const std::vector<PropertyNode> before = sheet.Nodes();
    ASSERT_NE(NodeAt(before, "$.data.intents[1].params"), nullptr);
    EXPECT_EQ(NodeAt(before, "$.data.intents[1].params")->Descriptor.Label, "Parameters, 1 of 4");
    for (int added = 0; added < 3; ++added)
        EXPECT_EQ(sheet.Act("$.data.intents[1].params", "add", ""), PropertyEditResult::Applied);
    const std::vector<PropertyNode> nodes = sheet.Nodes();
    const PropertyNode* full = NodeAt(nodes, "$.data.intents[1].params");
    ASSERT_NE(full, nullptr);
    EXPECT_EQ(full->Descriptor.Label, "Parameters, 4 of 4");
    EXPECT_EQ(std::ranges::find(full->Descriptor.Verbs, "add", &PropertyVerb::Id), full->Descriptor.Verbs.end());
    workspace.Sources.DiscardAll();
}
