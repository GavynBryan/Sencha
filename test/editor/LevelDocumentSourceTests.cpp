#include "WorkspaceFixture.h"

#include "documents/DocumentSourceSet.h"
#include "workspace/LevelDocumentSource.h"

#include <filesystem>
#include <fstream>
#include <memory>

// The open level as one document in the application's journal: every command
// is a step, a cleared history forgets the level's steps, the journal cancels
// open previews before it steps, a file changed on disk is a conflict rather
// than overwritten, and the level can be thrown away without saving.
namespace
{
struct CountingCommand final : ICommand
{
    CountingCommand(EditorDocument& document, int& count)
        : Document(document)
        , Count(count)
    {
    }
    void Execute() override
    {
        ++Count;
        Document.MarkDirty(true);
    }
    void Undo() override { --Count; }

    EditorDocument& Document;
    int& Count;
};

class LevelDocumentSourceTest : public WorkspaceTest
{
protected:
    void TearDown() override
    {
        Sources.DiscardAll();
        std::error_code ec;
        std::filesystem::remove_all(Dir, ec);
    }

    void Execute() { Commands.Execute(std::make_unique<CountingCommand>(Workspace.ActiveDocument(), Count)); }

    [[nodiscard]] std::filesystem::path SavedWorld()
    {
        std::filesystem::create_directories(Dir);
        const std::filesystem::path path = Dir / "level.sworld";
        EXPECT_TRUE(Workspace.World.SaveWorldAs(path.string()));
        return path;
    }

    std::filesystem::path Dir = std::filesystem::temp_directory_path() / "sencha_level_document_source";
    int Count = 0;
    DocumentSourceSet Sources;
    LevelDocumentSource Source{ Workspace, Commands, Sources };
};
}

TEST_F(LevelDocumentSourceTest, EachCommandIsAJournalStep)
{
    Execute();
    Execute();
    ASSERT_TRUE(Sources.CanUndo());
    Sources.Undo();
    EXPECT_EQ(Count, 1);
    Sources.Undo();
    EXPECT_EQ(Count, 0);
    EXPECT_FALSE(Sources.CanUndo());
    Sources.Redo();
    EXPECT_EQ(Count, 1);
}

TEST_F(LevelDocumentSourceTest, ClearingTheHistoryForgetsTheLevelsSteps)
{
    Execute();
    Commands.Clear();
    EXPECT_FALSE(Sources.CanUndo()) << "the journal kept a step the level can no longer retake";
}

TEST_F(LevelDocumentSourceTest, AJournalStepCancelsALivePreviewWithoutDroppingHistory)
{
    Execute();
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    Workspace.BeginInsetOnSelectedFaces(0.1f);
    ASSERT_TRUE(Workspace.HasPendingInset());
    ASSERT_EQ(Sources.ChangedDocuments().size(), 1u);

    Sources.Undo();
    EXPECT_FALSE(Workspace.HasPendingInset()) << "the preview was left staged under a step";
    EXPECT_EQ(Count, 0) << "the command before the preview was retaken";
}

TEST_F(LevelDocumentSourceTest, SavingOverAFileChangedOnDiskIsAConflict)
{
    const std::filesystem::path path = SavedWorld();
    Execute();
    std::ofstream(path, std::ios::app) << "\n";
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));

    EXPECT_EQ(Sources.Save(Source.Ref()).Status, DocumentSaveStatus::Conflict);
    std::string error;
    ASSERT_TRUE(Sources.Settle(Source.Ref(), ConflictChoice::KeepMine, error)) << error;
    EXPECT_FALSE(Workspace.World.IsExternallyModified());
    EXPECT_TRUE(Sources.ChangedDocuments().empty());
}

TEST_F(LevelDocumentSourceTest, AnUntitledLevelIsNotSavedBehindTheAuthorsBack)
{
    Execute();
    const DocumentSaveResult result = Sources.Save(Source.Ref());
    EXPECT_EQ(result.Status, DocumentSaveStatus::Failed);
    EXPECT_NE(result.Error.find("Save As"), std::string::npos) << result.Error;
}

TEST_F(LevelDocumentSourceTest, DiscardingLeavesNothingChangedAndNothingToUndo)
{
    Execute();
    Sources.DiscardAll();
    EXPECT_TRUE(Sources.ChangedDocuments().empty());
    EXPECT_FALSE(Sources.CanUndo());
    EXPECT_FALSE(Workspace.World.IsDirty());
}
