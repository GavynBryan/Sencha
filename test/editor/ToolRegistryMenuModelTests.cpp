#include "WorkspaceFixture.h"

#include "brush/BrushMesh.h"
#include "commands/CommandStack.h"
#include "document/EditorScene.h"
#include "document/tools/BrushTool.h"
#include "meshedit/MeshEditService.h"
#include "selection/SelectionService.h"
#include "tools/ToolContext.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolRegistryMenuModel.h"

// The registry as a radial menu: the adapter's mapping, and that a choice on
// it goes through the registry's own entry rules.
namespace
{
class SpyTool : public ITool
{
public:
    std::string_view GetId() const override { return "spy"; }
    std::string_view GetDisplayName() const override { return "Spy"; }
    IconId GetIcon() const override { return IconId::Cut; }
    void OnActivate(ToolContext&) override { ++Activations; }
    int Activations = 0;
};

class ToolRegistryMenuModelTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        auto spy = std::make_unique<SpyTool>();
        Spy = spy.get();
        Tools().Register(std::move(spy));
        SpyIndex = static_cast<int>(Tools().GetTools().size()) - 1;
    }
    [[nodiscard]] ToolRegistry& Tools() { return *Workspace.Interaction.Tools; }
    [[nodiscard]] int IndexOf(std::string_view id)
    {
        const auto& tools = Tools().GetTools();
        for (std::size_t i = 0; i < tools.size(); ++i)
            if (tools[i]->GetId() == id)
                return static_cast<int>(i);
        return -1;
    }
    SpyTool* Spy = nullptr;
    int SpyIndex = -1;
};
}

TEST_F(ToolRegistryMenuModelTest, ItemsAreTheToolsInRegistryOrder)
{
    ToolRegistryMenuModel menu(Tools());
    ASSERT_EQ(menu.Count(), static_cast<int>(Tools().GetTools().size()));
    for (int i = 0; i < menu.Count(); ++i)
    {
        const ITool& tool = *Tools().GetTools()[static_cast<std::size_t>(i)];
        EXPECT_EQ(menu.Item(i).Label, tool.GetDisplayName());
        EXPECT_EQ(menu.Item(i).Icon, tool.GetIcon());
    }
    EXPECT_EQ(menu.Item(99).Label, "");
}

TEST_F(ToolRegistryMenuModelTest, TheActiveEntryFollowsTheRegistry)
{
    ToolRegistryMenuModel menu(Tools());
    EXPECT_EQ(menu.ActiveIndex(), IndexOf("select"));
    ASSERT_TRUE(Tools().Activate("brush"));
    EXPECT_EQ(menu.ActiveIndex(), IndexOf("brush"));
}

TEST_F(ToolRegistryMenuModelTest, VariantsAreTheToolsOwn)
{
    ToolRegistryMenuModel menu(Tools());
    const int select = IndexOf("select");
    EXPECT_EQ(menu.Variants(select).size(), 4u);
    EXPECT_EQ(menu.Variants(select)[3].Label, "Face");
    EXPECT_EQ(menu.Variants(SpyIndex).size(), 0u);
    Workspace.MeshEdit.SetElementKind(MeshElementKind::Edge);
    EXPECT_EQ(menu.ActiveVariant(select), 2);
    EXPECT_EQ(menu.ActiveVariant(SpyIndex), -1);
    EXPECT_EQ(menu.ActiveVariant(99), -1);
}

TEST_F(ToolRegistryMenuModelTest, AChoiceEntersAToolThatIsNotActiveAndOnlyThen)
{
    ToolRegistryMenuModel menu(Tools());
    menu.Select(SpyIndex, -1);
    EXPECT_EQ(Tools().GetActiveIndex(), SpyIndex);
    EXPECT_EQ(Spy->Activations, 1);
    // Over the tool already on: not re-entered.
    menu.Select(SpyIndex, -1);
    EXPECT_EQ(Spy->Activations, 1);
    // The hub, or nonsense: nothing.
    menu.Select(-1, -1);
    menu.Select(99, 0);
    EXPECT_EQ(Tools().GetActiveIndex(), SpyIndex);
}

TEST_F(ToolRegistryMenuModelTest, AChoiceWithAVariantGoesThroughTheRegistrysVariantEntry)
{
    ToolRegistryMenuModel menu(Tools());
    const int brush = IndexOf("brush");
    ASSERT_TRUE(Tools().Activate("brush"));
    auto& brushTool = static_cast<BrushTool&>(*Tools().GetTools()[static_cast<std::size_t>(brush)]);
    ToolContext& ctx = Tools().GetContext();
    brushTool.SetPending(ctx, BrushTool::PendingBrush{ .Center = { 0.0, 0.5, 0.0 }, .HalfExtents = { 0.5, 0.5, 0.5 },
                                                       .DepthSign = 1.0f, .DragDepthHalf = 0.5f });
    ASSERT_TRUE(brushTool.HasPending());
    const EntityId placed = ctx.Selection.GetSelection().front().Entity;
    const std::size_t boxFaces = ctx.Scene.TryGetBrushMesh(placed)->Faces.size();

    menu.Select(brush, 2);
    // Placed as it stood; cylinders are what comes next.
    EXPECT_FALSE(brushTool.HasPending());
    EXPECT_EQ(brushTool.Creation.ActivePrimitive, BrushPrimitive::Cylinder);
    ASSERT_NE(ctx.Scene.TryGetBrushMesh(placed), nullptr);
    EXPECT_EQ(ctx.Scene.TryGetBrushMesh(placed)->Faces.size(), boxFaces);
    EXPECT_TRUE(Workspace.Commands.CanUndo());

    // A variant on a tool that is not active enters it first.
    ASSERT_TRUE(Tools().Activate("brush"));
    menu.Select(IndexOf("select"), 3);
    EXPECT_EQ(Tools().GetActiveTool()->GetId(), "select");
    EXPECT_EQ(Workspace.MeshEdit.GetElementKind(), MeshElementKind::Face);
}
