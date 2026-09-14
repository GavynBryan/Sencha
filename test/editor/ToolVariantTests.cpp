#include "WorkspaceFixture.h"

#include "brush/BrushMesh.h"
#include "commands/CommandStack.h"
#include "document/EditorScene.h"
#include "document/tools/BrushTool.h"
#include "selection/SelectionService.h"
#include "tools/ToolContext.h"
#include "meshedit/MeshEditService.h"
#include "meshedit/MeshElementKindTraits.h"
#include "tools/ToolRegistry.h"

// A tool's variants are one model over its existing mode state: the select
// tool's over MeshEditService's element kind, the brush tool's over its own
// primitive. Neither keeps a copy, so a change through any door is seen
// through every other.
namespace
{
class ToolVariantTest : public WorkspaceTest
{
protected:
    [[nodiscard]] ToolRegistry& Tools() { return *Workspace.Interaction.Tools; }
    [[nodiscard]] ITool& ToolById(std::string_view id)
    {
        for (const std::unique_ptr<ITool>& tool : Tools().GetTools())
            if (tool != nullptr && tool->GetId() == id)
                return *tool;
        ADD_FAILURE() << "tool not registered: " << id;
        std::abort();
    }
};
}

TEST_F(ToolVariantTest, SelectsVariantsAreTheElementKindsAndReadTheLiveMode)
{
    ITool& select = ToolById("select");
    const std::span<const ITool::Variant> variants = select.GetVariants();
    const auto& kinds = AllMeshElementKinds();
    ASSERT_EQ(variants.size(), kinds.size());
    for (std::size_t i = 0; i < kinds.size(); ++i)
    {
        EXPECT_EQ(variants[i].Label, Traits(kinds[i]).Label);
        EXPECT_NE(variants[i].Icon, IconId::None);
    }

    // The mode key's door and the variant's door open onto one kind.
    Workspace.MeshEdit.SetElementKind(MeshElementKind::Edge);
    EXPECT_EQ(select.GetActiveVariant(Tools().GetContext()), 2);
    select.SelectVariant(Tools().GetContext(), 3);
    EXPECT_EQ(Workspace.MeshEdit.GetElementKind(), MeshElementKind::Face);
    EXPECT_EQ(select.GetActiveVariant(Tools().GetContext()), 3);
    // Out of range changes nothing.
    select.SelectVariant(Tools().GetContext(), 99);
    EXPECT_EQ(Workspace.MeshEdit.GetElementKind(), MeshElementKind::Face);
}

TEST_F(ToolVariantTest, BrushsVariantsAreItsPrimitives)
{
    auto& brush = static_cast<BrushTool&>(ToolById("brush"));
    const std::span<const ITool::Variant> variants = brush.GetVariants();
    ASSERT_EQ(variants.size(), 3u);
    for (const ITool::Variant& variant : variants)
    {
        EXPECT_FALSE(variant.Label.empty());
        EXPECT_NE(variant.Icon, IconId::None);
    }
    EXPECT_EQ(brush.Creation.ActivePrimitive, BrushPrimitive::Box);
    EXPECT_EQ(brush.GetActiveVariant(Tools().GetContext()), 0);

    brush.SelectVariant(Tools().GetContext(), 2);
    EXPECT_EQ(brush.Creation.ActivePrimitive, BrushPrimitive::Cylinder);
    EXPECT_EQ(brush.GetActiveVariant(Tools().GetContext()), 2);
    // A change made on the settings directly is what the variant reports.
    brush.Creation.ActivePrimitive = BrushPrimitive::Plane;
    EXPECT_EQ(brush.GetActiveVariant(Tools().GetContext()), 1);
    brush.SelectVariant(Tools().GetContext(), 99);
    EXPECT_EQ(brush.Creation.ActivePrimitive, BrushPrimitive::Plane);
}

TEST_F(ToolVariantTest, TheRegistryEntersTheToolOrPlacesItsWorkBeforeSelecting)
{
    // Not the active tool: entered first, as Activate enters it (the brush's
    // entry drops the element mode), then the variant.
    Workspace.MeshEdit.SetElementKind(MeshElementKind::Face);
    ASSERT_EQ(Tools().GetActiveTool()->GetId(), "select");
    std::size_t brushIndex = 0;
    for (std::size_t i = 0; i < Tools().GetTools().size(); ++i)
        if (Tools().GetTools()[i]->GetId() == "brush")
            brushIndex = i;
    ASSERT_TRUE(Tools().SelectVariant(brushIndex, 1));
    EXPECT_EQ(Tools().GetActiveTool()->GetId(), "brush");
    EXPECT_EQ(Workspace.MeshEdit.GetElementKind(), MeshElementKind::Object);
    auto& brush = static_cast<BrushTool&>(ToolById("brush"));
    EXPECT_EQ(brush.Creation.ActivePrimitive, BrushPrimitive::Plane);

    // The active tool with a brush staged: the brush is placed as it stands,
    // and only the next one is a cylinder.
    ToolContext& ctx = Tools().GetContext();
    brush.SetPending(ctx, BrushTool::PendingBrush{ .Center = { 0.0, 0.5, 0.0 }, .HalfExtents = { 0.5, 0.5, 0.5 },
                                                   .DepthSign = 1.0f, .DragDepthHalf = 0.5f });
    ASSERT_TRUE(brush.HasPending());
    const EntityId placed = ctx.Selection.GetSelection().front().Entity;
    const std::size_t planeFaces = ctx.Scene.TryGetBrushMesh(placed)->Faces.size();
    ASSERT_TRUE(Tools().SelectVariant(brushIndex, 2));
    EXPECT_FALSE(brush.HasPending());
    EXPECT_EQ(brush.Creation.ActivePrimitive, BrushPrimitive::Cylinder);
    ASSERT_NE(ctx.Scene.TryGetBrushMesh(placed), nullptr);
    EXPECT_EQ(ctx.Scene.TryGetBrushMesh(placed)->Faces.size(), planeFaces);
    EXPECT_TRUE(Workspace.Commands.CanUndo());

    // Out of range is refused and changes nothing.
    EXPECT_FALSE(Tools().SelectVariant(brushIndex, 99));
    EXPECT_FALSE(Tools().SelectVariant(99, 0));
    EXPECT_EQ(brush.Creation.ActivePrimitive, BrushPrimitive::Cylinder);
}

TEST_F(ToolVariantTest, AToolWithoutVariantsAnswersEmptyAndIgnoresASelection)
{
    for (const std::unique_ptr<ITool>& tool : Tools().GetTools())
    {
        ASSERT_NE(tool, nullptr);
        if (tool->GetId() == "select" || tool->GetId() == "brush")
            continue;
        EXPECT_TRUE(tool->GetVariants().empty()) << tool->GetId();
        EXPECT_EQ(tool->GetActiveVariant(Tools().GetContext()), -1) << tool->GetId();
        tool->SelectVariant(Tools().GetContext(), 0);
        EXPECT_EQ(tool->GetActiveVariant(Tools().GetContext()), -1) << tool->GetId();
    }
}
