#include "WorkspaceFixture.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "brush/BrushMesh.h"
#include "workspace/ElementVerbs.h"
#include "workspace/FaceUvSource.h"
#include "workspace/SelectionSummary.h"

#include <gtest/gtest.h>

#include <algorithm>

// What can be done to the selected elements is a set of actions that say when
// they apply, and the parametric ones read settings that also drive a preview
// while one is staged. The selection facet names the selection and lists them.
namespace
{
class ElementVerbsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Verbs.Offer(Catalog, Context);
        Uv.Offer(Catalog, Context);
        Layer = Stack.Push(Context, ActionRank::Workspace);
    }

    PropertyEditResult Commit(IPropertySource& source, std::string_view key, std::string_view value)
    {
        return source.BeginEdit(key) ? source.CommitEdit(key, value) : PropertyEditResult::Refused;
    }

    [[nodiscard]] bool Lists(const WorkspaceSelectionFacet& facet, std::string_view action) const
    {
        return std::ranges::find(facet.Actions, action) != facet.Actions.end();
    }

    ElementVerbs Verbs{ Workspace, {} };
    FaceUvSource Uv{ Workspace };
    ActionCatalog Catalog;
    ActionContext Context;
    ActionContextStack Stack;
    ActionContextStack::Layer Layer;
};
} // namespace

TEST_F(ElementVerbsTest, NothingSelectedHasNoCard)
{
    EXPECT_TRUE(SelectionOf(Workspace, Verbs, Uv).Title.empty());
    EXPECT_FALSE(Stack.Query("brush.normals").Enabled);
}

TEST_F(ElementVerbsTest, EachElementKindOffersItsOwnVerbs)
{
    const EntityId brush = AddBrush({ 0, 0, 0 });
    SelectEntity(brush);
    WorkspaceSelectionFacet facet = SelectionOf(Workspace, Verbs, Uv);
    EXPECT_FALSE(facet.Title.empty());
    EXPECT_TRUE(Lists(facet, "brush.normals"));
    EXPECT_TRUE(Stack.Query("brush.normals").Enabled);
    EXPECT_FALSE(Stack.Query("brush.merge").Enabled) << "merging needs two brushes";

    Workspace.MeshEdit.SetElementKind(MeshElementKind::Face);
    SelectElements(brush, MeshElementKind::Face, { 0, 1 });
    facet = SelectionOf(Workspace, Verbs, Uv);
    EXPECT_EQ(facet.Title, "2 faces");
    EXPECT_TRUE(Lists(facet, "face.inset"));
    EXPECT_TRUE(Lists(facet, "uv.fit"));
    ASSERT_EQ(facet.Sections.size(), 2u) << "the inset setting and the texture";
    EXPECT_EQ(facet.Sections[1].Heading, "Texture");

    Workspace.MeshEdit.SetElementKind(MeshElementKind::Edge);
    SelectElements(brush, MeshElementKind::Edge, { 0 });
    facet = SelectionOf(Workspace, Verbs, Uv);
    EXPECT_EQ(facet.Title, "1 edge");
    EXPECT_TRUE(Lists(facet, "edge.bevel"));
    EXPECT_FALSE(Stack.Query("edge.bridge").Enabled) << "a bridge spans two edges";
}

TEST_F(ElementVerbsTest, AStagedInsetFollowsItsSettingUntilAppliedOrCancelled)
{
    const EntityId brush = AddBrush({ 0, 0, 0 });
    Workspace.MeshEdit.SetElementKind(MeshElementKind::Face);
    SelectElements(brush, MeshElementKind::Face, { 0 });

    ASSERT_TRUE(Stack.Invoke("face.inset"));
    ASSERT_TRUE(Workspace.HasPendingInset());
    const WorkspaceSelectionFacet staged = SelectionOf(Workspace, Verbs, Uv);
    EXPECT_TRUE(staged.Staged);
    EXPECT_EQ(staged.Actions, (std::vector<std::string>{ "element.apply", "element.cancel" }));
    EXPECT_FALSE(Stack.Query("face.inset").Enabled) << "one staged edit at a time";

    EXPECT_EQ(Commit(Verbs, "inset", "0.5"), PropertyEditResult::Applied);
    EXPECT_EQ(Commit(Verbs, "inset", "deep"), PropertyEditResult::Refused);
    ASSERT_TRUE(Stack.Invoke("element.cancel"));
    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.CanUndo()) << "a cancelled preview leaves no step";
}

TEST_F(ElementVerbsTest, TheFacesTextureIsEditedAsOneStep)
{
    const EntityId brush = AddBrush({ 0, 0, 0 });
    Workspace.MeshEdit.SetElementKind(MeshElementKind::Face);
    SelectElements(brush, MeshElementKind::Face, { 0, 1 });

    EXPECT_EQ(Commit(Uv, "scale", "4, 3"), PropertyEditResult::Applied);
    EXPECT_EQ(Commit(Uv, "scale", "4"), PropertyEditResult::Refused) << "a scale has two numbers";
    const BrushMesh* mesh = Scene().TryGetBrushMesh(brush);
    ASSERT_NE(mesh, nullptr);
    EXPECT_FLOAT_EQ(mesh->Faces[0].Material.Uv.Scale.X, 4.0f);
    EXPECT_FLOAT_EQ(mesh->Faces[1].Material.Uv.Scale.Y, 3.0f);
    ASSERT_TRUE(Commands.CanUndo());
    Commands.Undo();
    EXPECT_FLOAT_EQ(Scene().TryGetBrushMesh(brush)->Faces[0].Material.Uv.Scale.X, 2.0f);
}
