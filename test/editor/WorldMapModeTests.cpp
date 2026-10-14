#include "WorkspaceFixture.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "workspace/WorldMapMode.h"
#include "workspace/PartitionOutline.h"

#include <SDL3/SDL_keycode.h>

#include <gtest/gtest.h>

// The world map is up or down as one state: its action toggles it, Escape puts
// it down, and only while it is up does it take the pointer from the views.
namespace
{
class WorldMapModeTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Mode.Offer(Catalog, Context);
        Layer = Stack.Push(Context, ActionRank::Workspace);
    }

    PartitionOutline Partition{ Workspace };
    WorldMapMode Mode{ Workspace, Partition, DocumentRef{} };
    ActionCatalog Catalog;
    ActionContext Context;
    ActionContextStack Stack;
    ActionContextStack::Layer Layer;
};
} // namespace

TEST_F(WorldMapModeTest, ItsActionPutsItUpAndEscapeTakesItDown)
{
    ASSERT_TRUE(Workspace.World.IsWorld());
    EXPECT_EQ(Mode.Content(), LevelContent::Views);
    EXPECT_EQ(Mode.OnInput(PointerMoveEvent{ .Position = { 10.0f, 10.0f }, .Delta = {}, .Modifiers = {} }), InputConsumed::No);

    ASSERT_TRUE(Stack.Invoke(WorldMapMode::kAction));
    EXPECT_EQ(Mode.Content(), LevelContent::Map);
    EXPECT_TRUE(Stack.Query(WorldMapMode::kAction).Checked);
    EXPECT_EQ(Mode.OnInput(PointerMoveEvent{ .Position = { 10.0f, 10.0f }, .Delta = {}, .Modifiers = {} }), InputConsumed::Yes)
        << "while up, the map has the pointer";

    EXPECT_EQ(Mode.OnInput(KeyDownEvent{ .Key = SDLK_ESCAPE, .Modifiers = {} }), InputConsumed::Yes);
    EXPECT_EQ(Mode.Content(), LevelContent::Views);
}

TEST_F(WorldMapModeTest, ALoneLevelHasNoMapButItsTitleStillReads)
{
    Workspace.World.New();
    ASSERT_FALSE(Workspace.World.IsWorld());
    EXPECT_EQ(Mode.Title(), "World") << "asked every frame, a lone level has no manifest to read";
    EXPECT_FALSE(Stack.Query(WorldMapMode::kAction).Enabled);
    EXPECT_EQ(Mode.LayOut(Rect2d::FromMinMax({ 0, 0 }, { 100, 100 }), true), nullptr);
}
