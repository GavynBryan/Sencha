#include "WorkspaceFixture.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "render/ShadowResidencyReadout.h"
#include "workspace/LightingDiagnostics.h"

#include <gtest/gtest.h>

#include <algorithm>

// The lighting said where it applies: a selected light's shadow is its card's
// lighting section, a light the budget left unshadowed is a problem that selects
// it, and the baked preview is a view action whose staleness is reported.
namespace
{
class LightingDiagnosticsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Shadows.Active = true;
        Shadows.FocusRegistry = Registry();
        Shadows.Budgets.MaxSlots = 1;
        Light = AddBrush({ 0.0, 0.0, 0.0 });
        Lighting.Offer(Catalog, Context);
        Layer = Stack.Push(Context, ActionRank::Workspace);
    }

    void HoldSpot(EntityId entity, std::uint32_t asked, std::uint32_t granted)
    {
        Shadows.Rows.push_back({ .Entity = entity, .Type = GpuLightType::Spot, .TileSize = asked, .Held = true,
                                 .Slot = 0 });
        Shadows.Slots[0].Live = true;
        Shadows.Slots[0].EverRendered = true;
        Shadows.Slots[0].Allocation.Size = granted;
        ++Shadows.Stats.Spot.HeldRequests;
    }

    void DenySpot(EntityId entity)
    {
        Shadows.Rows.push_back({ .Entity = entity, .Type = GpuLightType::Spot, .TileSize = 512, .Held = false });
        ++Shadows.Stats.Spot.DeniedRequests;
    }

    [[nodiscard]] static const PropertyNode* Find(const std::vector<PropertyNode>& nodes, std::string_view key)
    {
        for (const PropertyNode& node : nodes)
        {
            if (node.Key == key)
                return &node;
            if (const PropertyNode* child = Find(node.Children, key))
                return child;
        }
        return nullptr;
    }

    ShadowResidencyReadout Shadows;
    BakedLightingPreview Preview = BakedLightingPreview::Unavailable;
    LightingDiagnostics Lighting{ Workspace, Commands,
                                  { .Shadows = &Shadows,
                                    .Preview = [this] { return Preview; },
                                    .ShowPreview = [this](bool on) {
                                        Preview = on ? BakedLightingPreview::Fresh : BakedLightingPreview::Off;
                                    } } };
    EntityId Light;
    ActionCatalog Catalog;
    ActionContext Context;
    ActionContextStack Stack;
    ActionContextStack::Layer Layer;
};
} // namespace

TEST_F(LightingDiagnosticsTest, ASelectedLightsShadowIsItsCardsLightingSection)
{
    HoldSpot(Light, 1024, 512);
    EXPECT_TRUE(Lighting.Nodes().empty()) << "nothing selected";
    SelectEntity(Light);
    const std::vector<PropertyNode> nodes = Lighting.Nodes();
    const PropertyNode* tier = Find(nodes, "tier");
    ASSERT_NE(tier, nullptr);
    EXPECT_EQ(tier->Value, "1024 -> 512 (496 usable)") << "the tier asked for and the one the atlas had room for";
    EXPECT_TRUE(tier->Descriptor.ReadOnly);
    ASSERT_NE(Find(nodes, "state"), nullptr);
    EXPECT_EQ(Find(nodes, "state")->Value, "Rendered");
    EXPECT_FALSE(Lighting.BeginEdit("tier"));
}

TEST_F(LightingDiagnosticsTest, AnObjectThatIsNoLightHasNoLightingSection)
{
    HoldSpot(Light, 512, 512);
    SelectEntity(AddBrush({ 4.0, 0.0, 0.0 }));
    EXPECT_TRUE(Lighting.Nodes().empty());
}

TEST_F(LightingDiagnosticsTest, ALightTheBudgetLeftUnshadowedIsAProblemThatSelectsIt)
{
    DenySpot(Light);
    const std::vector<Problem> problems = Lighting.Problems({});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems.front().Severity, ProblemSeverity::Warning);
    EXPECT_NE(problems.front().Message.find("render.shadow.max_spot"), std::string::npos);
    ASSERT_TRUE(problems.front().Location.starts_with("entity:"));

    Workspace.Selection.ClearSelection();
    ASSERT_TRUE(Lighting.Reveal(problems.front().Location));
    EXPECT_EQ(Workspace.Selection.GetPrimarySelection().Entity, Light);
    EXPECT_FALSE(Lighting.Reveal("zone:0000000000000001")) << "another source's location";
}

TEST_F(LightingDiagnosticsTest, TheBakedPreviewIsAViewActionAndAStaleOneIsReported)
{
    EXPECT_FALSE(Stack.Query("view.baked_lighting").Enabled) << "nothing cooked yet";
    Preview = BakedLightingPreview::Off;
    EXPECT_TRUE(Stack.Query("view.baked_lighting").Enabled);
    EXPECT_FALSE(Stack.Query("view.baked_lighting").Checked);
    ASSERT_TRUE(Stack.Invoke("view.baked_lighting"));
    EXPECT_TRUE(Stack.Query("view.baked_lighting").Checked);
    EXPECT_TRUE(Lighting.Problems({}).empty());

    Preview = BakedLightingPreview::Stale;
    const std::vector<Problem> problems = Lighting.Problems({});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems.front().Severity, ProblemSeverity::Info);
}
