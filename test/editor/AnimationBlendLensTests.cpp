// The Blends lens over the moving example: the blendspace its base layer plays
// as a pad shows it and moves it, the card's account of blending, and the
// deck's lane of blends.

#include "properties/PropertyTree.h"
#include "EditorDocumentsFixture.h"
#include "authoring/AnimationBlendProperties.h"
#include "authoring/AnimationBlendspacePad.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationTimelineLanes.h"
#include "presenters/PlanePad.h"

#include "AnimationExampleContent.h"

#include <gtest/gtest.h>

namespace
{
constexpr const char* kRig = "asset://animation/examples/moving.rig.sdata";

} // namespace

TEST(PlanePad, APointMapsAcrossAndUpWithinTheInsetAndBack)
{
    const Rect2d region(Vec2d(100.0f, 50.0f), Vec2d(228.0f, 228.0f));
    const Vec2d corner = PlanePadPointOf(region, Vec2d(0.0f, 0.0f));
    const Vec2d far = PlanePadPointOf(region, Vec2d(1.0f, 1.0f));
    EXPECT_LT(corner.Y, region.Position.Y + region.Height()) << "the bottom of the plane is inside the pad";
    EXPECT_GT(far.X, corner.X);
    EXPECT_LT(far.Y, corner.Y) << "up is up";
    const Vec2d middle = PlanePadValueAt(region, false, PlanePadPointOf(region, Vec2d(0.25f, 0.75f)));
    EXPECT_NEAR(middle.X, 0.25f, 1e-5f);
    EXPECT_NEAR(middle.Y, 0.75f, 1e-5f);
    EXPECT_EQ(PlanePadValueAt(region, false, Vec2d(0.0f, 0.0f)), Vec2d(0.0f, 1.0f)) << "held to its edges";
    EXPECT_EQ(PlanePadValueAt(region, true, far).Y, 0.5f) << "a line has one range";
}

TEST(AnimationBlendLens, ThePadMovesTheBlendspaceAndTheCardAndLaneAccountForIt)
{
    AnimationExampleContent content;
    EditorDocuments documents(content.Assets);
    AnimationPreviewWorkspace workspace(content.Assets, documents.Sources, documents.Store);
    ASSERT_TRUE(workspace.OpenRig(kRig)) << workspace.Rig.Error;
    AnimationPreviewSession& session = workspace.Rig.Simulation;
    session.RunTo(60);

    const std::optional<AnimationBlendspaceView> moving = AnimationBlendspaceOn(session, 0);
    ASSERT_TRUE(moving.has_value()) << "moving at speed 1 plays the blendspace";
    EXPECT_EQ(moving->Axes, 1u);
    EXPECT_EQ(moving->AxisFact[0], "Speed");
    ASSERT_EQ(moving->Samples.size(), 2u);
    EXPECT_DOUBLE_EQ(moving->Samples[0].U, 0.25);
    EXPECT_DOUBLE_EQ(moving->Samples[1].U, 0.75);
    EXPECT_NEAR(moving->U, 0.25, 1e-6);
    EXPECT_FLOAT_EQ(moving->Samples[0].Weight, 1.0f);

    // Dragging the pad halfway sets Speed to 2 for the next tick.
    ASSERT_TRUE(PlaceAnimationBlendspacePoint(session, 0, 0.5, 0.5));
    session.Step();
    const std::optional<AnimationBlendspaceView> halfway = AnimationBlendspaceOn(session, 0);
    ASSERT_TRUE(halfway.has_value());
    EXPECT_NEAR(halfway->Coordinates[0], 2.0f, 1e-5f);
    EXPECT_NEAR(halfway->Samples[0].Weight, 0.5f, 1e-5f);
    EXPECT_FALSE(workspace.Sources.CanUndo()) << "a live fact, never an asset change";

    AnimationBlendProperties card(workspace);
    const std::vector<PropertyNode> nodes = card.Nodes();
    ASSERT_NE(FindProperty(nodes, "now.0"), nullptr);
    ASSERT_NE(FindProperty(nodes, "blendspace.at"), nullptr);
    EXPECT_EQ(FindProperty(nodes, "blendspace.at")->Value, "Speed = 2.00");
    ASSERT_NE(FindProperty(nodes, "recent.0"), nullptr) << "idle to moving blended";
    EXPECT_NE(FindProperty(card.Comparison().Nodes(), "ab"), nullptr);

    const TimelineLane lane = AnimationBlendLane(session, 0);
    ASSERT_FALSE(lane.Spans.empty());
    EXPECT_GT(lane.Spans[0].End, lane.Spans[0].Start);
    const std::optional<AnimationLanePick> pick = ParseAnimationLaneKey(lane.Spans[0].Key);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->Names, AnimationLanePick::What::Blend);
    EXPECT_FALSE(AnimationBlendspaceOn(session, 3).has_value());
    session.Close();
}
