#include "WorkspaceFixture.h"

#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"

#include <gtest/gtest.h>

// Delete removes whatever is selected as one step, and says why when some of
// it cannot go, rather than doing nothing.
namespace
{
using Result = EditorWorkspace::Deletion::Result;

class DeleteSelectionTest : public WorkspaceTest
{
};
} // namespace

TEST_F(DeleteSelectionTest, NothingSelectedDeletesNothingAndSaysNothing)
{
    const EditorWorkspace::Deletion deletion = Workspace.DeleteSelection();
    EXPECT_EQ(deletion.Outcome, Result::NothingSelected);
    EXPECT_TRUE(deletion.WhyNot.empty());
    EXPECT_FALSE(Workspace.Commands.CanUndo());
}

TEST_F(DeleteSelectionTest, ObjectsAndAnotherBrushsFacesGoTogetherAsOneStep)
{
    const EntityId doomed = AddBrush({ 0, 0, 0 });
    const EntityId kept = AddBrush({ 4, 0, 0 });
    const std::size_t faces = Scene().TryGetBrushMesh(kept)->Faces.size();
    const std::size_t entities = Scene().GetAllEntities().size();
    std::vector<SelectableRef> selection = { SelectableRef::EntitySelection(Registry(), doomed) };
    selection.push_back(RefsOf(kept, MeshElementKind::Face).front());
    Select(selection);

    const EditorWorkspace::Deletion deletion = Workspace.DeleteSelection();
    ASSERT_EQ(deletion.Outcome, Result::Deleted) << deletion.WhyNot;
    EXPECT_FALSE(Scene().HasEntity(doomed)) << "a face selected beside it kept the object from going";
    ASSERT_NE(Scene().TryGetBrushMesh(kept), nullptr);
    EXPECT_EQ(Scene().TryGetBrushMesh(kept)->Faces.size(), faces - 1);

    Workspace.Commands.Undo();
    EXPECT_EQ(Scene().GetAllEntities().size(), entities);
    EXPECT_EQ(Scene().TryGetBrushMesh(kept)->Faces.size(), faces) << "one undo restores both";
}

TEST_F(DeleteSelectionTest, AnObjectsOwnElementsGoWithIt)
{
    const EntityId brush = AddBrush({ 0, 0, 0 });
    std::vector<SelectableRef> selection = { SelectableRef::EntitySelection(Registry(), brush) };
    selection.push_back(RefsOf(brush, MeshElementKind::Edge).front());
    Select(selection);

    const EditorWorkspace::Deletion deletion = Workspace.DeleteSelection();
    EXPECT_EQ(deletion.Outcome, Result::Deleted);
    EXPECT_TRUE(deletion.WhyNot.empty()) << deletion.WhyNot;
    EXPECT_FALSE(Scene().HasEntity(brush));
}

TEST_F(DeleteSelectionTest, EdgesThatCannotDissolveSayWhyAndChangeNothing)
{
    // A lone plane's edge bounds one face: there is nothing to merge it with.
    Transform3f at = Transform3f::Identity();
    const EntityId brush = Scene().CreateBrushFromMesh(at, BrushOps::MakePlane({ 1, 0, 1 }, 1));
    const std::size_t faces = Scene().TryGetBrushMesh(brush)->Faces.size();
    Select({ RefsOf(brush, MeshElementKind::Edge).front() });

    const EditorWorkspace::Deletion deletion = Workspace.DeleteSelection();
    EXPECT_EQ(deletion.Outcome, Result::Refused);
    EXPECT_NE(deletion.WhyNot.find("can't be dissolved"), std::string::npos) << deletion.WhyNot;
    EXPECT_EQ(Scene().TryGetBrushMesh(brush)->Faces.size(), faces);
    EXPECT_FALSE(Workspace.Commands.CanUndo());
}

TEST_F(DeleteSelectionTest, VerticesAreRefusedWithTheWayToDoIt)
{
    const EntityId brush = AddBrush({ 0, 0, 0 });
    Select({ RefsOf(brush, MeshElementKind::Vertex).front() });

    const EditorWorkspace::Deletion deletion = Workspace.DeleteSelection();
    EXPECT_EQ(deletion.Outcome, Result::Refused);
    EXPECT_NE(deletion.WhyNot.find("weld"), std::string::npos) << deletion.WhyNot;
}
