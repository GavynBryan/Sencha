// A level edit made as a change set (an operation, an accepted draft, their
// undo and redo) re-resolves the selection, so nothing it deleted stays selected.

#include "WorkspaceFixture.h"

#include "document/EntityChangeSet.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"

#include <gtest/gtest.h>

namespace
{
class LevelEditSelectionTest : public WorkspaceTest
{
protected:
    EntityId Resolve(const std::string& id)
    {
        return ResolveEntitySubject(Workspace.ActiveDocument(), id).Entities.front();
    }

    void Delete(const std::string& id)
    {
        const EditorOperationOutcome deleted = Workspace.Level.Mutate(
            "test.delete", JsonValue{}, [id](EditorDocument& document, EntityChangeRecorder& recorder) {
                const EntityId entity = ResolveEntitySubject(document, id).Entities.front();
                recorder.Touch(entity);
                document.GetScene().DestroyEntity(entity);
                return EditorOperationOutcome::Ok({});
            });
        ASSERT_EQ(deleted.Status, EditorOperationStatus::Ok);
    }
};
}

TEST_F(LevelEditSelectionTest, NothingDeletedStaysSelected)
{
    const EntityId brush = AddBrush({ 0, 0, 0 });
    const std::string id = PersistentIdText(Workspace.ActiveDocument(), brush);
    SelectEntity(brush);
    Delete(id);
    EXPECT_TRUE(Workspace.Selection.GetSelection().empty()) << "deleted by an operation";

    Commands.Undo();
    SelectEntity(Resolve(id));
    Commands.Redo();
    EXPECT_TRUE(Workspace.Selection.GetSelection().empty()) << "deleted again by redo";
}
