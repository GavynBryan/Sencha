#include "WorkspaceFixture.h"

#include "editmodes/ManipulatorSession.h"
#include "editmodes/TransformModeItems.h"
#include "editmodes/TransformModeMenuModel.h"

// The gizmo modes as a radial menu: the shared mode table as entries, the
// mode the session is presently driving as the active one, a choice as a
// request for that mode.
namespace
{
class TransformModeMenuModelTest : public WorkspaceTest
{
protected:
    [[nodiscard]] ManipulatorSession& Session() { return *Workspace.Interaction.Manipulators; }
    [[nodiscard]] TransformModeMenuModel Menu() { return TransformModeMenuModel([this] { return Workspace.Interaction.Manipulators; }); }
};
}

TEST_F(TransformModeMenuModelTest, ItemsAreTheModeTableInOrder)
{
    TransformModeMenuModel menu = Menu();
    ASSERT_EQ(menu.Count(), static_cast<int>(kTransformModeItems.size()));
    for (int i = 0; i < menu.Count(); ++i)
    {
        EXPECT_EQ(menu.Item(i).Label, kTransformModeItems[static_cast<std::size_t>(i)].Choice.Label);
        EXPECT_NE(menu.Item(i).Icon, IconId::None);
    }
    EXPECT_TRUE(menu.Variants(0).empty());
    EXPECT_EQ(menu.ActiveVariant(0), -1);
}

TEST_F(TransformModeMenuModelTest, TheActiveEntryIsTheModeBeingDriven)
{
    TransformModeMenuModel menu = Menu();
    Session().SetTransformMode(TransformMode::Rotate);
    EXPECT_EQ(menu.ActiveIndex(), 2);
    // Resize with nothing resizable selected reads as Move, as the toolbar
    // shows it: the menu says what the editor is doing.
    Session().SetTransformMode(TransformMode::Resize);
    EXPECT_EQ(Session().GetTransformMode(), TransformMode::Resize);
    int expected = -1;
    for (std::size_t i = 0; i < kTransformModeItems.size(); ++i)
        if (kTransformModeItems[i].Mode == Session().EffectiveMode())
            expected = static_cast<int>(i);
    EXPECT_EQ(menu.ActiveIndex(), expected);
}

TEST_F(TransformModeMenuModelTest, AChoiceRequestsThatMode)
{
    TransformModeMenuModel menu = Menu();
    menu.Select(3, -1);
    EXPECT_EQ(Session().GetTransformMode(), TransformMode::Scale);
    menu.Select(1, 7); // a variant it has none of is ignored, the entry is not
    EXPECT_EQ(Session().GetTransformMode(), TransformMode::Move);
    menu.Select(-1, -1);
    menu.Select(99, -1);
    EXPECT_EQ(Session().GetTransformMode(), TransformMode::Move);
    // Without a session there is nothing to say or do.
    TransformModeMenuModel orphan([] { return static_cast<ManipulatorSession*>(nullptr); });
    EXPECT_EQ(orphan.ActiveIndex(), -1);
    orphan.Select(0, -1);
}
