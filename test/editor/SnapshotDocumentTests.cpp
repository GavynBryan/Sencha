// A value a session holds and edits in place, kept in a file: every change is
// a step of the one journal, an edit that previews is one step or none, and
// the file is written by a save and read back by a revert.

#include "documents/DocumentSourceSet.h"
#include "documents/SnapshotDocument.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace
{
struct SnapshotDocuments : testing::Test
{
    std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("sencha_snapshot_document_" + std::to_string(std::random_device{}()));
    // What the session holds; "!" is a value it refuses.
    std::string Held = "a";
    DocumentSourceSet Journal;
    SnapshotDocument Document{ Journal,
                               { .Write =
                                     [this](std::string& text, std::string&) {
                                         text = Held;
                                         return true;
                                     },
                                 .Read =
                                     [this](std::string_view text, std::string& error) {
                                         if (text == "!")
                                         {
                                             error = "refused";
                                             return false;
                                         }
                                         Held = text;
                                         return true;
                                     } } };

    void SetUp() override
    {
        std::filesystem::create_directories(Root);
        Document.Open(File());
    }
    void TearDown() override
    {
        Journal.DiscardAll();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    [[nodiscard]] std::filesystem::path File() const { return Root / "value.txt"; }
    void Edit(std::string value)
    {
        Held = std::move(value);
        Document.Changed();
    }
    [[nodiscard]] std::string OnDisk() const
    {
        std::ifstream in(File());
        return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    }
    // Stamped later so the timestamp alone cannot miss it.
    void WriteOnDisk(const std::string& text) const
    {
        std::ofstream(File(), std::ios::trunc) << text;
        std::filesystem::last_write_time(File(), std::filesystem::last_write_time(File()) + std::chrono::seconds(5));
    }
};
} // namespace

TEST_F(SnapshotDocuments, EachChangeIsAStepTheJournalTakesBackAndRedoes)
{
    EXPECT_FALSE(Document.IsChanged()) << "what the holder had when it opened is what it holds";
    Edit("b");
    Edit("b");
    Edit("c");
    EXPECT_TRUE(Document.IsChanged());
    EXPECT_EQ(Journal.ChangedDocuments(), std::vector<DocumentRef>{ Document.Ref() });

    Journal.Undo();
    EXPECT_EQ(Held, "b") << "telling it of no change took no step";
    Journal.Undo();
    EXPECT_EQ(Held, "a");
    EXPECT_FALSE(Document.IsChanged());
    EXPECT_FALSE(Journal.CanUndo());
    Journal.Redo();
    EXPECT_EQ(Held, "b");
}

TEST_F(SnapshotDocuments, AnEditIsOneStepOrNone)
{
    Document.BeginEdit();
    Edit("b");
    Edit("c");
    EXPECT_FALSE(Journal.CanUndo()) << "nothing is a step while the edit is open";
    EXPECT_TRUE(Document.IsChanged()) << "an open edit is a change";
    Document.CommitEdit();
    Journal.Undo();
    EXPECT_EQ(Held, "a") << "the whole edit was one step";

    Document.BeginEdit();
    Edit("d");
    Document.CancelEdit();
    EXPECT_EQ(Held, "a") << "a cancel puts back what the edit found";

    Document.BeginEdit();
    Edit("e");
    Journal.Undo();
    EXPECT_EQ(Held, "a") << "undo cancels the edit in flight first";
    EXPECT_FALSE(Document.IsEditing());
}

TEST_F(SnapshotDocuments, ASaveWritesTheFileAndAChangeOnDiskIsAConflict)
{
    Edit("b");
    ASSERT_EQ(Journal.Save(Document.Ref()).Status, DocumentSaveStatus::Saved);
    EXPECT_EQ(OnDisk(), "b");
    EXPECT_FALSE(Document.IsChanged());

    Edit("c");
    WriteOnDisk("x");
    EXPECT_EQ(Journal.Save(Document.Ref()).Status, DocumentSaveStatus::Conflict) << "never an overwrite";
    std::string error;
    ASSERT_TRUE(Journal.Settle(Document.Ref(), ConflictChoice::TakeFile, error)) << error;
    EXPECT_EQ(Held, "x");
    EXPECT_FALSE(Document.IsChanged());
    Journal.Undo();
    EXPECT_EQ(Held, "c") << "the file's version was taken as a step";

    WriteOnDisk("y");
    ASSERT_TRUE(Journal.Settle(Document.Ref(), ConflictChoice::KeepMine, error)) << error;
    EXPECT_EQ(OnDisk(), "c");
}

TEST_F(SnapshotDocuments, ARevertTakesTheFilesVersionAndARefusedOneChangesNothing)
{
    Edit("b");
    std::string error;
    EXPECT_FALSE(Document.Revert(error)) << "no file yet";
    ASSERT_EQ(Journal.Save(Document.Ref()).Status, DocumentSaveStatus::Saved);
    Edit("c");
    ASSERT_TRUE(Document.Revert(error)) << error;
    EXPECT_EQ(Held, "b");

    WriteOnDisk("!");
    EXPECT_FALSE(Document.Revert(error));
    EXPECT_EQ(error, "refused");
    EXPECT_EQ(Held, "b");
}

TEST_F(SnapshotDocuments, ADiscardPutsTheSavedVersionBackAndForgetsItsSteps)
{
    Edit("b");
    Document.DiscardDocument(Document.Ref().Key);
    EXPECT_EQ(Held, "a");
    EXPECT_FALSE(Document.IsChanged());
    EXPECT_FALSE(Journal.CanUndo());
    EXPECT_EQ(Document.DocumentLabel(Document.Ref().Key), "value.txt");
}
