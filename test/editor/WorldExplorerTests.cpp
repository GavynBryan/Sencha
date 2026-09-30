#include "WorkspaceFixture.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "document/commands/ReparentEntitiesCommand.h"
#include "workspace/EntityVerbs.h"
#include "workspace/WorldExplorer.h"

#include <gtest/gtest.h>

#include <algorithm>

// The scene as an outline: entities under their parents, branches that open
// and close, selection by one, by range and beside the others, and marks that
// hide and lock -- each change a step. The entity verbs parent and unparent
// the selection.
namespace
{
class WorldExplorerTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Parent = AddBrush({ 0, 0, 0 });
        Child = AddBrush({ 2, 0, 0 });
        Other = AddBrush({ 4, 0, 0 });
        const EntityId children[] = { Child };
        Commands.Execute(MakeReparentEntitiesCommand(children, Parent, ReparentTransformRule::KeepWorld, Scene(),
                                                     Workspace.World.FocusDocument()));
        Verbs.Offer(Catalog, Context);
        Layer = Stack.Push(Context, ActionRank::Workspace);
    }

    [[nodiscard]] ExplorerPage All() { return Explorer.Explore({ .Explorer = "world", .Count = 100 }); }

    [[nodiscard]] const ExplorerItem* ItemFor(const ExplorerPage& page, EntityId entity)
    {
        const auto found = std::ranges::find(page.Items, std::to_string(entity.Index) + ":" + std::to_string(entity.Generation),
                                             &ExplorerItem::Key);
        return found != page.Items.end() ? &*found : nullptr;
    }

    [[nodiscard]] bool IsSelected(EntityId entity) const
    {
        return std::ranges::any_of(Workspace.Selection.GetSelection(),
                                   [&](const SelectableRef& ref) { return ref.IsEntity() && ref.Entity == entity; });
    }

    EntityId Parent{};
    EntityId Child{};
    EntityId Other{};
    WorldExplorer Explorer{ Workspace };
    EntityVerbs Verbs{ Workspace, {} };
    ActionCatalog Catalog;
    ActionContext Context;
    ActionContextStack Stack;
    ActionContextStack::Layer Layer;
};
} // namespace

TEST_F(WorldExplorerTest, ChildrenSitUnderTheirParentAndABranchCloses)
{
    ExplorerPage page = All();
    const ExplorerItem* parent = ItemFor(page, Parent);
    const ExplorerItem* child = ItemFor(page, Child);
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(parent->Branch, ExplorerBranch::Open);
    EXPECT_EQ(child->Depth, parent->Depth + 1);

    Explorer.Choose(parent->Key, ExplorerChoice::Open, 0);
    page = All();
    EXPECT_EQ(ItemFor(page, Parent)->Branch, ExplorerBranch::Closed);
    EXPECT_EQ(ItemFor(page, Child), nullptr) << "a closed branch shows nothing under it";
}

TEST_F(WorldExplorerTest, AFilterKeepsWhatMatchesAndTheBranchesAboveIt)
{
    const ExplorerPage everything = All();
    const std::string childName = ItemFor(everything, Child)->Label;
    const ExplorerPage page = Explorer.Explore({ .Explorer = "world", .Filter = childName, .Count = 100 });
    EXPECT_NE(ItemFor(page, Child), nullptr);
    EXPECT_NE(ItemFor(page, Parent), nullptr) << "the match's parent stays so it is not shown at the root";
}

TEST_F(WorldExplorerTest, PickingSelectsOneARangeOrOneMore)
{
    const ExplorerPage page = All();
    Explorer.Choose(ItemFor(page, Parent)->Key, ExplorerChoice::Pick, 0);
    EXPECT_TRUE(IsSelected(Parent));
    Explorer.Choose(ItemFor(page, Other)->Key, ExplorerChoice::Extend, 0);
    EXPECT_TRUE(IsSelected(Child)) << "a range takes the rows between";
    EXPECT_TRUE(IsSelected(Other));
    Explorer.Choose(ItemFor(page, Child)->Key, ExplorerChoice::Toggle, 0);
    EXPECT_FALSE(IsSelected(Child));
    EXPECT_TRUE(IsSelected(Parent));
}

TEST_F(WorldExplorerTest, AMarkHidesOrLocksAsAStep)
{
    const ExplorerPage page = All();
    Explorer.Choose(ItemFor(page, Other)->Key, ExplorerChoice::Mark, 0);
    EXPECT_FALSE(Scene().IsEntityVisible(Other));
    Explorer.Choose(ItemFor(page, Other)->Key, ExplorerChoice::Mark, 1);
    EXPECT_TRUE(Scene().IsEntityLocked(Other));
    Commands.Undo();
    Commands.Undo();
    EXPECT_TRUE(Scene().IsEntityVisible(Other));
    EXPECT_FALSE(Scene().IsEntityLocked(Other));
}

TEST_F(WorldExplorerTest, ParentingPutsTheRestOfTheSelectionUnderThePrimary)
{
    Select({ SelectableRef::EntitySelection(Registry(), Other), SelectableRef::EntitySelection(Registry(), Parent) });
    ASSERT_TRUE(Stack.Query("entity.parent").Enabled);
    ASSERT_TRUE(Stack.Invoke("entity.parent"));
    EXPECT_EQ(Scene().GetParent(Other), Parent);

    Select({ SelectableRef::EntitySelection(Registry(), Other) });
    ASSERT_TRUE(Stack.Invoke("entity.unparent"));
    EXPECT_FALSE(Scene().GetParent(Other).IsValid());
}
