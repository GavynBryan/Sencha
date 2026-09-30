#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "resources/ActiveResources.h"
#include "resources/ResourceActions.h"

#include <gtest/gtest.h>

// The stash is worked through actions, so a slot on screen, a palette entry and
// a key all do the same thing -- to whichever kind is primary when they run.
namespace
{
class ResourceActionsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        OfferResourceActions(Catalog, Context, Held, [this] { return Primary; });
        Layer = Stack.Push(Context, ActionRank::Workspace);
    }

    AssetType Primary = AssetType::Material;
    ActiveResources Held;
    ActionCatalog Catalog;
    ActionContext Context;
    ActionContextStack Stack;
    ActionContextStack::Layer Layer;
};
} // namespace

TEST_F(ResourceActionsTest, NothingHeldLeavesOnlyNothingToDo)
{
    EXPECT_FALSE(Stack.Query("resource.slot.1").Enabled);
    EXPECT_FALSE(Stack.Query("resource.store.1").Enabled);
    EXPECT_FALSE(Stack.Query("resource.clear.1").Enabled);
}

TEST_F(ResourceActionsTest, StoringUsingAndClearingASlotWorkTheHeldKind)
{
    Held.SetActive(AssetRef{ AssetType::Material, "materials/brick.smat" });
    ASSERT_TRUE(Stack.Invoke("resource.store.2"));
    EXPECT_EQ(Held.Stash(AssetType::Material)[1].Path, "materials/brick.smat");
    EXPECT_TRUE(Stack.Query("resource.clear.2").Enabled);

    Held.SetActive(AssetRef{ AssetType::Material, "materials/steel.smat" });
    ASSERT_TRUE(Stack.Invoke("resource.slot.2"));
    EXPECT_EQ(Held.Active(AssetType::Material).Path, "materials/brick.smat");

    ASSERT_TRUE(Stack.Invoke("resource.clear.2"));
    EXPECT_FALSE(Held.Stash(AssetType::Material)[1].IsValid());
}

TEST_F(ResourceActionsTest, TheSlotsFollowWhicheverKindIsPrimary)
{
    Held.SetActive(AssetRef{ AssetType::Material, "materials/brick.smat" });
    Primary = AssetType::Scene;
    EXPECT_FALSE(Stack.Query("resource.store.1").Enabled);
    Held.SetActive(AssetRef{ AssetType::Scene, "scenes/crate.sscene" });
    ASSERT_TRUE(Stack.Invoke("resource.store.1"));
    EXPECT_EQ(Held.Stash(AssetType::Scene)[0].Path, "scenes/crate.sscene");
    EXPECT_FALSE(Held.Stash(AssetType::Material)[0].IsValid());
}
