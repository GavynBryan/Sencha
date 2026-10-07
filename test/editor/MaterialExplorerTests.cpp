#include "WorkspaceFixture.h"

#include "brush/BrushMesh.h"
#include "workspace/MaterialExplorer.h"

#include <gtest/gtest.h>

#include <string>

// The project's materials as the shelf offers them: material files, the one in
// hand marked, and choosing one puts it in hand -- or, put to use, on the
// selected faces.
namespace
{
class MaterialExplorerTest : public WorkspaceTest
{
protected:
    MaterialExplorer Explorer{ Workspace };
};
} // namespace

TEST_F(MaterialExplorerTest, OffersMaterialFilesWithTheOneInHandCurrent)
{
    Workspace.Resources.SetActive(AssetRef{ AssetType::Material, "asset://materials/stone.smat" });
    const WorkspaceExplorerInfo info = Explorer.Info();
    EXPECT_EQ(info.Shape, ExplorerShape::Files);
    ASSERT_EQ(info.Accepts.size(), 1u);
    EXPECT_EQ(info.Accepts.front().Extension, ".smat");
    EXPECT_EQ(info.Current, "asset://materials/stone.smat");
    EXPECT_TRUE(info.Withheld.empty());
}

TEST_F(MaterialExplorerTest, PickingPutsItInHandAndPuttingItToUseAppliesIt)
{
    const EntityId brush = AddBrush({ 0, 0, 0 });
    Workspace.MeshEdit.SetElementKind(MeshElementKind::Face);
    SelectElements(brush, MeshElementKind::Face, { 0 });

    const std::string tile = "asset://materials/tile.smat";
    Explorer.Choose(tile, ExplorerChoice::Pick);
    EXPECT_EQ(Workspace.Resources.Active(AssetType::Material).Path, tile);
    EXPECT_EQ(Explorer.Info().Current, tile);
    EXPECT_FALSE(Commands.CanUndo()) << "picking is not an edit";

    Explorer.Choose(tile, ExplorerChoice::Commit);
    EXPECT_EQ(Scene().TryGetBrushMesh(brush)->Faces[0].Material.Material.Path, tile);
    EXPECT_TRUE(Commands.CanUndo());
}
