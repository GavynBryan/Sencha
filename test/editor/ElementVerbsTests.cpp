#include "WorkspaceFixture.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "authoring/ComponentPropertySource.h"
#include "brush/BrushMesh.h"
#include "document/EntityNameComponent.h"
#include "properties/PropertyEditSession.h"
#include "workspace/ElementVerbs.h"
#include "workspace/EntityVerbs.h"
#include "workspace/FaceUvSource.h"
#include "workspace/LightingDiagnostics.h"
#include "workspace/PartitionOutline.h"
#include "workspace/SelectionSummary.h"

#include <gtest/gtest.h>

#include <world/identity/PersistentIdComponent.h>

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
    FaceUvSource Uv{ Workspace, nullptr };
    EntityVerbs Entities{ Workspace, {} };
    PartitionOutline Partition{ Workspace };
    LightingDiagnostics Lighting{ Workspace, Commands, {} };
    ComponentPropertySource Components{ Workspace.World, Workspace.Selection, Commands,
                                        Workspace.Affordances->Registry() };
    ActionCatalog Catalog;
    ActionContext Context;
    ActionContextStack Stack;
    ActionContextStack::Layer Layer;
};
} // namespace

TEST_F(ElementVerbsTest, NothingSelectedHasNoCard)
{
    EXPECT_TRUE(SelectionOf(Workspace, Verbs, Entities, Uv, Components, Partition, Lighting).Title.empty());
    EXPECT_FALSE(Stack.Query("brush.normals").Enabled);
}

TEST_F(ElementVerbsTest, AnObjectThatIsNoBrushOffersNoBrushVerbs)
{
    SelectEntity(Scene().CreateEntity({ 0, 2, 0 }));
    const WorkspaceSelectionFacet facet = SelectionOf(Workspace, Verbs, Entities, Uv, Components, Partition, Lighting);
    EXPECT_TRUE(Lists(facet, "edit.duplicate"));
    EXPECT_FALSE(Lists(facet, "brush.normals"));
    EXPECT_FALSE(Lists(facet, "brush.bake"));
}

TEST_F(ElementVerbsTest, EachElementKindOffersItsOwnVerbs)
{
    const EntityId brush = AddBrush({ 0, 0, 0 });
    SelectEntity(brush);
    WorkspaceSelectionFacet facet = SelectionOf(Workspace, Verbs, Entities, Uv, Components, Partition, Lighting);
    EXPECT_FALSE(facet.Title.empty());
    EXPECT_TRUE(Lists(facet, "brush.normals"));
    ASSERT_EQ(facet.Details.size(), 1u) << "one object's components are there to look at closely";
    EXPECT_TRUE(Stack.Query("brush.normals").Enabled);
    EXPECT_FALSE(Stack.Query("brush.merge").Enabled) << "merging needs two brushes";

    Workspace.MeshEdit.SetElementKind(MeshElementKind::Face);
    SelectElements(brush, MeshElementKind::Face, { 0, 1 });
    facet = SelectionOf(Workspace, Verbs, Entities, Uv, Components, Partition, Lighting);
    EXPECT_EQ(facet.Title, "2 faces");
    EXPECT_TRUE(Lists(facet, "face.inset"));
    EXPECT_TRUE(Lists(facet, "uv.fit"));
    ASSERT_EQ(facet.Sections.size(), 2u) << "the inset setting and the texture";
    EXPECT_EQ(facet.Sections[1].Heading, "Texture");

    Workspace.MeshEdit.SetElementKind(MeshElementKind::Edge);
    SelectElements(brush, MeshElementKind::Edge, { 0 });
    facet = SelectionOf(Workspace, Verbs, Entities, Uv, Components, Partition, Lighting);
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
    const WorkspaceSelectionFacet staged = SelectionOf(Workspace, Verbs, Entities, Uv, Components, Partition, Lighting);
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

// One object's title is its name, renamed by typing over it, and the line
// under it says how else it is known; the name and the id are not repeated in
// the list below.
TEST_F(ElementVerbsTest, OneObjectIsTitledByItsNameAndKnownByItsId)
{
    Components.ShowElsewhere({ ResolveComponentTypeId<EntityNameComponent>(),
                               ResolveComponentTypeId<PersistentIdComponent>() });
    const EntityId entity = Scene().CreateEntity({ 0, 2, 0 });
    SelectEntity(entity);
    const WorkspaceSelectionFacet facet = SelectionOf(Workspace, Verbs, Entities, Uv, Components, Partition, Lighting);
    EXPECT_NE(facet.Subtitle.find("id "), std::string::npos) << facet.Subtitle;
    ASSERT_EQ(facet.TitleProperty.Source, &Components);
    ASSERT_FALSE(facet.TitleProperty.Key.empty());

    for (const PropertyNode& node : Components.Nodes())
    {
        EXPECT_NE(node.Key, "name") << "the name is the title";
        EXPECT_NE(node.Key, "persistent_id") << "the id is the subtitle";
    }

    EXPECT_EQ(PropertyEditSession{}.Set(Components, facet.TitleProperty.Key, "Gate"), PropertyEditResult::Applied);
    EXPECT_EQ(SelectionOf(Workspace, Verbs, Entities, Uv, Components, Partition, Lighting).Title, "Gate");
    Commands.Undo();
    EXPECT_NE(SelectionOf(Workspace, Verbs, Entities, Uv, Components, Partition, Lighting).Title, "Gate");
}
