#include "WorkspaceFixture.h"

#include "thumbnails/AssetPictures.h"
#include "tools/ToolRegistry.h"
#include "workspace/HeldResources.h"
#include "workspace/MaterialExplorer.h"
#include "workspace/PrefabExplorer.h"

#include <gtest/gtest.h>

// The hands hold what the tool in hand consumes -- a material unless it says
// otherwise, a prefab for the Entity tool -- and the assets to hand are the one
// in use and then those put by, each once.
TEST_F(WorkspaceTest, TheHandsHoldWhatTheToolConsumes)
{
    AssetPictures pictures;
    EXPECT_EQ(HeldType(Workspace), AssetType::Material);
    EXPECT_EQ(HeldResourceFacet(Workspace, pictures).Explorer, MaterialExplorer::kId);

    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("place"));
    EXPECT_EQ(HeldType(Workspace), AssetType::Scene);
    const WorkspaceResourceFacet prefab = HeldResourceFacet(Workspace, pictures);
    EXPECT_EQ(prefab.Explorer, PrefabExplorer::kId);
    EXPECT_EQ(prefab.Stash.size(), ActiveResources::kStashSlots);
}

TEST_F(WorkspaceTest, TheAssetsToHandAreTheOneInUseThenThosePutByEachOnce)
{
    AssetPictures pictures;
    const AssetRef stone{ AssetType::Material, "asset://materials/stone.smat" };
    const AssetRef brick{ AssetType::Material, "asset://materials/brick.smat" };
    Workspace.Resources.SetActive(stone);
    Workspace.Resources.StoreInStash(AssetType::Material, 0);
    Workspace.Resources.SetActive(brick);
    Workspace.Resources.StoreInStash(AssetType::Material, 1);

    const std::vector<ContextMenuModel::AssetChoice> choices = AssetsToHand(Workspace, AssetType::Material, pictures);
    ASSERT_EQ(choices.size(), 2u) << "the active brick is also put by, and listed once";
    EXPECT_EQ(choices[0].Path, brick.Path);
    EXPECT_EQ(choices[1].Path, stone.Path);
}
