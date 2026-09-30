// One journal and one save-all across every kind of open document: steps are
// retaken newest first by document identity, never by bare key.

#include "documents/DocumentSourceSet.h"
#include "FakeDocumentSource.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace
{
    struct TwoSources : testing::Test
    {
        std::vector<std::string> Log;
        DocumentSourceSet Set;
        FakeDocumentSource Data{ "data", Set, Log };
        FakeDocumentSource Events{ "events", Set, Log };

        // The contract every owner keeps: nothing is destroyed holding changes.
        void TearDown() override { Set.DiscardAll(); }

        std::vector<std::string> Drain() { return std::exchange(Log, {}); }
    };

    using Strings = std::vector<std::string>;
}

TEST_F(TwoSources, UndoRetakesTheNewestStepAcrossSources)
{
    Data.Edit("rig");
    Events.Edit("walk");
    Data.Edit("rig");
    Set.Undo();
    Set.Undo();
    Set.Undo();
    EXPECT_FALSE(Set.CanUndo());
    EXPECT_EQ(Drain(), (Strings{ "data:rig:undo", "events:walk:undo", "data:rig:undo" }));
    Set.Redo();
    Set.Redo();
    EXPECT_EQ(Drain(), (Strings{ "data:rig:redo", "events:walk:redo" }));
}

TEST_F(TwoSources, TheSameKeyInTwoSourcesStaysTwoDocuments)
{
    Data.Edit("same");
    Events.Edit("same");
    Set.ForgetDocument({ &Data, "same" });
    Set.Undo();
    EXPECT_EQ(Drain(), (Strings{ "events:same:undo" }));
    EXPECT_FALSE(Set.CanUndo());
}

TEST_F(TwoSources, AnOpenEditIsCancelledBeforeAnyStep)
{
    Data.Edit("rig");
    Events.Documents["walk"].Editing = true;
    Set.Undo();
    EXPECT_EQ(Drain(), (Strings{ "events:walk:cancel", "data:rig:undo" }));
}

TEST_F(TwoSources, ForgettingADocumentKeepsTheOthersInOrder)
{
    Data.Edit("a");
    Events.Edit("walk");
    Data.Edit("b");
    Data.Edit("a");
    Set.Undo();
    Set.Undo();
    (void)Drain();

    Set.ForgetDocument({ &Data, "a" });
    EXPECT_TRUE(Set.CanRedo()) << "b's step is still ahead of the cursor";
    Set.Redo();
    EXPECT_FALSE(Set.CanRedo());
    Set.Undo();
    Set.Undo();
    EXPECT_FALSE(Set.CanUndo());
    EXPECT_EQ(Drain(), (Strings{ "data:b:redo", "data:b:undo", "events:walk:undo" }));
}

TEST_F(TwoSources, RemovingASourceDropsItsSteps)
{
    std::vector<std::string> log;
    {
        FakeDocumentSource scratch{ "scratch", Set, log };
        Data.Edit("rig");
        scratch.Edit("x");
        Events.Edit("walk");
        scratch.Edit("x");
        scratch.Documents["x"].SaveAs = DocumentSaveStatus::Conflict;
        (void)Set.SaveAll();
        scratch.Documents.clear();
    }
    EXPECT_EQ(Set.LastSave().Results.size(), 2u);
    EXPECT_TRUE(Set.LastSave().WithStatus(DocumentSaveStatus::Conflict).empty());
    Set.Undo();
    Set.Undo();
    EXPECT_FALSE(Set.CanUndo());
    EXPECT_EQ(Drain(), (Strings{ "events:walk:undo", "data:rig:undo" }));
}

TEST_F(TwoSources, ANewStepDropsWhatCouldBeRedone)
{
    Data.Edit("rig");
    Events.Edit("walk");
    Set.Undo();
    Data.Edit("rig");
    EXPECT_FALSE(Set.CanRedo());
    Set.Undo();
    Set.Undo();
    EXPECT_EQ(Drain(), (Strings{ "events:walk:undo", "data:rig:undo", "data:rig:undo" }));
}

TEST_F(TwoSources, ARecordWhileSteppingAsserts)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    Data.Edit("rig");
    Data.RecordWhileStepping = true;
    EXPECT_DEBUG_DEATH(Set.Undo(), "recorded a new step while the journal was retaking one");
}

TEST_F(TwoSources, SaveAllClassifiesEachDocumentAndALaterSaveReplacesIt)
{
    Data.Edit("saved");
    Data.Edit("broken");
    Data.Documents["broken"].SaveAs = DocumentSaveStatus::Failed;
    Events.Edit("theirs");
    Events.Documents["theirs"].SaveAs = DocumentSaveStatus::Conflict;
    Events.Documents["typing"].Editing = true;
    Data.Documents["clean"] = {};

    const DocumentSaveReport& report = Set.SaveAll();
    EXPECT_EQ(report.Results.size(), 4u) << "a clean document is not saved";
    ASSERT_EQ(report.WithStatus(DocumentSaveStatus::Failed).size(), 1u);
    EXPECT_EQ(report.WithStatus(DocumentSaveStatus::Failed)[0]->Error, "disk full");
    EXPECT_EQ(report.WithStatus(DocumentSaveStatus::Conflict)[0]->Document, (DocumentRef{ &Events, "theirs" }));
    EXPECT_EQ(report.WithStatus(DocumentSaveStatus::Saved).size(), 2u);

    Data.Documents["broken"].SaveAs = DocumentSaveStatus::Saved;
    (void)Set.Save({ &Data, "broken" });
    EXPECT_EQ(Set.LastSave().Results.size(), 4u);
    EXPECT_TRUE(Set.LastSave().WithStatus(DocumentSaveStatus::Failed).empty());
}

TEST_F(TwoSources, SettlingRoutesToTheOwningSourceAndClearsTheConflict)
{
    Data.Edit("same");
    Events.Edit("same");
    Data.Documents["same"].SaveAs = DocumentSaveStatus::Conflict;
    Events.Documents["same"].SaveAs = DocumentSaveStatus::Conflict;
    (void)Set.SaveAll();
    ASSERT_EQ(Set.LastSave().WithStatus(DocumentSaveStatus::Conflict).size(), 2u);

    std::string error;
    ASSERT_TRUE(Set.Settle({ &Events, "same" }, ConflictChoice::TakeFile, error));
    EXPECT_EQ(Drain(), (Strings{ "events:same:file" }));
    const auto conflicts = Set.LastSave().WithStatus(DocumentSaveStatus::Conflict);
    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0]->Document, (DocumentRef{ &Data, "same" }));
    EXPECT_EQ(Set.ChangedDocuments(), (std::vector<DocumentRef>{ { &Data, "same" } }));
}

TEST_F(TwoSources, DiscardingAllClosesEveryChangedDocumentAndItsSteps)
{
    Data.Edit("rig");
    Events.Edit("walk");
    Events.Documents["typing"].Editing = true;
    Data.Documents["clean"] = {};
    Set.DiscardAll();
    EXPECT_TRUE(Set.ChangedDocuments().empty());
    EXPECT_FALSE(Set.CanUndo());
    EXPECT_EQ(Drain(), (Strings{ "data:rig:discard", "events:typing:discard", "events:walk:discard" }));
    EXPECT_EQ(Data.Documents.count("clean"), 1u) << "a clean document is left open";
}

TEST_F(TwoSources, TheStepObserverHearsOfEachStepBeforeItIsTaken)
{
    std::vector<DocumentRef> observed;
    Set.SetStepObserver([&](const DocumentRef& document) {
        observed.push_back(document);
        Log.push_back("observed");
    });
    Data.Edit("rig");
    Events.Edit("walk");
    Set.Undo();
    Set.Redo();
    EXPECT_EQ(observed, (std::vector<DocumentRef>{ { &Events, "walk" }, { &Events, "walk" } }));
    EXPECT_EQ(Drain(), (Strings{ "observed", "events:walk:undo", "observed", "events:walk:redo" }));
}

TEST_F(TwoSources, DestroyingASourceThatStillHasChangesIsAnError)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const auto destroyChanged = [this] {
        std::vector<std::string> log;
        FakeDocumentSource scratch{ "scratch", Set, log };
        scratch.Edit("x");
    };
    EXPECT_DEBUG_DEATH(destroyChanged(), "destroyed with changes nobody saved or discarded");
}
