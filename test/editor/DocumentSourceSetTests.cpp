// One journal and one save-all across every kind of open document: steps are
// retaken newest first by document identity, never by bare key.

#include "documents/DocumentSourceSet.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace
{
    struct FakeDocument
    {
        bool Changed = false;
        bool Editing = false;
        DocumentSaveStatus SaveAs = DocumentSaveStatus::Saved;
    };

    class FakeSource final : public DocumentSource
    {
    public:
        FakeSource(std::string name, DocumentSourceSet& set, std::vector<std::string>& log)
            : Name(std::move(name)), Set(set), Log(log)
        {
            Set.AddSource(*this);
        }
        ~FakeSource() override { Set.RemoveSource(*this); }

        void Edit(const std::string& key)
        {
            Documents[key].Changed = true;
            Set.Record({ this, key });
        }

        void AppendChangedDocuments(std::vector<DocumentRef>& out) override
        {
            for (const auto& [key, document] : Documents)
                if (document.Changed || document.Editing)
                    out.push_back({ this, key });
        }

        DocumentSaveResult SaveDocument(std::string_view key) override
        {
            FakeDocument& document = Documents[std::string(key)];
            document.Editing = false;
            if (document.SaveAs == DocumentSaveStatus::Saved)
                document.Changed = false;
            return { {}, document.SaveAs, document.SaveAs == DocumentSaveStatus::Failed ? "disk full" : "" };
        }

        bool SettleDocument(std::string_view key, ConflictChoice choice, std::string&) override
        {
            Log.push_back(Name + ":" + std::string(key) + (choice == ConflictChoice::KeepMine ? ":mine" : ":file"));
            Documents[std::string(key)] = {};
            return true;
        }

        void StepDocument(std::string_view key, DocumentStep step) override
        {
            Log.push_back(Name + ":" + std::string(key) + (step == DocumentStep::Undo ? ":undo" : ":redo"));
            if (RecordWhileStepping)
                Set.Record({ this, std::string(key) });
        }

        void CancelDocumentEdits() override
        {
            for (auto& [key, document] : Documents)
                if (std::exchange(document.Editing, false))
                    Log.push_back(Name + ":" + key + ":cancel");
        }

        std::string Name;
        DocumentSourceSet& Set;
        std::vector<std::string>& Log;
        std::map<std::string, FakeDocument> Documents;
        bool RecordWhileStepping = false;
    };

    struct TwoSources : testing::Test
    {
        std::vector<std::string> Log;
        DocumentSourceSet Set;
        FakeSource Data{ "data", Set, Log };
        FakeSource Events{ "events", Set, Log };

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
        FakeSource scratch{ "scratch", Set, log };
        Data.Edit("rig");
        scratch.Edit("x");
        Events.Edit("walk");
        scratch.Edit("x");
        scratch.Documents["x"].SaveAs = DocumentSaveStatus::Conflict;
        (void)Set.SaveAll();
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
