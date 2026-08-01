#include "WorkspaceFixture.h"

#include "meshedit/FaceMaterialEdits.h"

// The workspace-level texturing verbs: what the clipboard holds, what a failed
// copy leaves behind, and what reaches the undo stack. The projection math is
// covered by FaceMaterialEditsTests.
namespace
{

class WorkspaceMaterialProjectionTest : public WorkspaceTest
{
protected:
    [[nodiscard]] AssetRef MaterialOfFace(EntityId entity, std::uint32_t face)
    {
        const BrushMesh* mesh = Scene().TryGetBrushMesh(entity);
        EXPECT_NE(mesh, nullptr);
        if (mesh == nullptr || face >= mesh->Faces.size())
            return {};
        return mesh->Faces[face].Material.Material;
    }

    // A material the apply verbs will treat as active.
    static AssetRef TestMaterial() { return AssetRef{ AssetType::Material, "materials/test.smat" }; }
};

TEST_F(WorkspaceMaterialProjectionTest, ApplyingWithNoActiveMaterialIsANoOp)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    ASSERT_FALSE(Workspace.ActiveMaterial.Active.IsValid());

    Workspace.ApplyActiveMaterialToSelectedFaces();

    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(WorkspaceMaterialProjectionTest, ApplyingSetsTheSelectedFaceMaterialAsAnUndoStep)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    Workspace.ActiveMaterial.Active = TestMaterial();

    Workspace.ApplyActiveMaterialToSelectedFaces();

    EXPECT_EQ(MaterialOfFace(brush, 0).Path, TestMaterial().Path);
    ASSERT_TRUE(Commands.CanUndo());

    Commands.Undo();
    EXPECT_NE(MaterialOfFace(brush, 0).Path, TestMaterial().Path);
}

TEST_F(WorkspaceMaterialProjectionTest, ApplyingWithNoFaceSelectedIsANoOp)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectEntity(brush);
    Workspace.ActiveMaterial.Active = TestMaterial();

    Workspace.ApplyActiveMaterialToSelectedFaces();

    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(WorkspaceMaterialProjectionTest, CopyingAFaceFillsTheClipboard)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    Workspace.ActiveMaterial.Active = TestMaterial();
    Workspace.ApplyActiveMaterialToSelectedFaces();

    Workspace.CopySelectedFaceProjection();

    ASSERT_TRUE(Workspace.UvClipboard.has_value());
    EXPECT_EQ(Workspace.UvClipboard->Material.Path, TestMaterial().Path);
}

// A copy that resolves nothing must leave a good clipboard intact rather than
// clearing it.
TEST_F(WorkspaceMaterialProjectionTest, AFailedCopyKeepsThePreviousClipboard)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    Workspace.ActiveMaterial.Active = TestMaterial();
    Workspace.ApplyActiveMaterialToSelectedFaces();
    Workspace.CopySelectedFaceProjection();
    ASSERT_TRUE(Workspace.UvClipboard.has_value());

    SelectEntity(brush);
    Workspace.CopySelectedFaceProjection();

    ASSERT_TRUE(Workspace.UvClipboard.has_value());
    EXPECT_EQ(Workspace.UvClipboard->Material.Path, TestMaterial().Path);
}

TEST_F(WorkspaceMaterialProjectionTest, PastingCarriesTheMaterialToAnotherFace)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    Workspace.ActiveMaterial.Active = TestMaterial();
    Workspace.ApplyActiveMaterialToSelectedFaces();
    Workspace.CopySelectedFaceProjection();

    SelectElements(brush, MeshElementKind::Face, { 1 });
    Workspace.PasteFaceProjectionToSelection();

    EXPECT_EQ(MaterialOfFace(brush, 1).Path, TestMaterial().Path);
}

TEST_F(WorkspaceMaterialProjectionTest, PastingAnEmptyClipboardIsANoOp)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    ASSERT_FALSE(Workspace.UvClipboard.has_value());

    Workspace.PasteFaceProjectionToSelection();

    EXPECT_FALSE(Commands.CanUndo());
}

// The clipboard is editor-wide view state, so rebuilding the editing stack for
// a new document must not discard it.
TEST_F(WorkspaceMaterialProjectionTest, TheClipboardSurvivesAnInteractionReset)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    Workspace.ActiveMaterial.Active = TestMaterial();
    Workspace.ApplyActiveMaterialToSelectedFaces();
    Workspace.CopySelectedFaceProjection();
    ASSERT_TRUE(Workspace.UvClipboard.has_value());

    Workspace.ResetInteractionState();

    ASSERT_TRUE(Workspace.UvClipboard.has_value());
    EXPECT_EQ(Workspace.UvClipboard->Material.Path, TestMaterial().Path);
    EXPECT_EQ(Workspace.ActiveMaterial.Active.Path, TestMaterial().Path);
}

} // namespace
