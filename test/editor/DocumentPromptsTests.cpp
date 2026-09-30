#include "prompts/DocumentPrompts.h"
#include "problems/ProblemList.h"
#include "FakeDocumentSource.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace
{
struct SaveFollowUp : testing::Test
{
    std::vector<std::string> Log;
    DocumentSourceSet Sources;
    ProblemList Problems;
    PromptQueue Prompts;
    FakeDocumentSource Levels{ "level", Sources, Log };
    DocumentSaveFollowUp FollowUp{ Sources, Problems, Prompts };

    // A source may not go with changes nobody settled.
    void TearDown() override { Sources.DiscardAll(); }

    DocumentRef Ref(const std::string& key) { return { &Levels, key }; }
    void SaveAs(const std::string& key, DocumentSaveStatus status)
    {
        Levels.Edit(key);
        Levels.Documents[key].SaveAs = status;
        (void)Sources.Save(Ref(key));
    }
};
} // namespace

TEST(DocumentPrompts, ADocumentWithNoChangesProceedsWithoutAsking)
{
    PromptQueue prompts;
    PromptQueue::Asker asker = prompts.MakeAsker();
    std::optional<DirtyDisposition> chosen;
    AskAboutChanges(asker, false, "glass.sscene", [&](DirtyDisposition d) { chosen = d; });
    EXPECT_EQ(chosen, DirtyDisposition::Refuse);
    EXPECT_EQ(prompts.Front(), nullptr);

    chosen.reset();
    AskAboutChanges(asker, true, "glass.sscene", [&](DirtyDisposition d) { chosen = d; });
    ASSERT_NE(prompts.Front(), nullptr);
    EXPECT_FALSE(chosen.has_value()) << "nothing proceeds until the question is answered";
    EXPECT_TRUE(prompts.Answer("discard"));
    EXPECT_EQ(chosen, DirtyDisposition::Discard);

    chosen.reset();
    AskAboutChanges(asker, true, "glass.sscene", [&](DirtyDisposition d) { chosen = d; });
    EXPECT_TRUE(prompts.Dismiss());
    EXPECT_FALSE(chosen.has_value()) << "cancelling proceeds with nothing";
}

TEST_F(SaveFollowUp, AFailedSaveIsAProblemUntilASaveSucceeds)
{
    SaveAs("a", DocumentSaveStatus::Failed);
    EXPECT_EQ(Problems.Count(ProblemSeverity::Error), 1u);
    EXPECT_EQ(Prompts.Front(), nullptr) << "a failure is reported, not asked about";

    SaveAs("a", DocumentSaveStatus::Saved);
    EXPECT_EQ(Problems.Count(ProblemSeverity::Info), 0u);
}

TEST_F(SaveFollowUp, AProblemGoesWithItsDocument)
{
    SaveAs("a", DocumentSaveStatus::SavedWithProblems);
    EXPECT_EQ(Problems.Count(ProblemSeverity::Warning), 1u);
    Levels.DiscardDocument("a");
    EXPECT_EQ(Problems.Count(ProblemSeverity::Info), 0u);
}

TEST_F(SaveFollowUp, AConflictAsksWhichVersionToKeep)
{
    SaveAs("a", DocumentSaveStatus::Conflict);
    ASSERT_NE(Prompts.Front(), nullptr);
    EXPECT_EQ(Problems.Count(ProblemSeverity::Warning), 1u);

    EXPECT_TRUE(Prompts.Answer("keep"));
    EXPECT_EQ(Log.back(), "level:a:mine");
    EXPECT_EQ(Problems.Count(ProblemSeverity::Info), 0u) << "settled, the conflict is gone";
}

TEST_F(SaveFollowUp, ALaterDecisionIsAskedAgainAtTheNextSave)
{
    SaveAs("a", DocumentSaveStatus::Conflict);
    EXPECT_TRUE(Prompts.Dismiss());
    EXPECT_EQ(Problems.Count(ProblemSeverity::Warning), 1u) << "undecided, it stays a problem";

    SaveAs("b", DocumentSaveStatus::Saved);
    EXPECT_EQ(Prompts.Front(), nullptr) << "another document's save does not nag";

    SaveAs("a", DocumentSaveStatus::Conflict);
    ASSERT_NE(Prompts.Front(), nullptr);
    SaveAs("a", DocumentSaveStatus::Conflict);
    EXPECT_TRUE(Prompts.Dismiss());
    EXPECT_EQ(Prompts.Front(), nullptr) << "one question per conflict, however often it is saved";
}

TEST_F(SaveFollowUp, ASettleThatFailsIsAProblemUntilTheConflictGoes)
{
    SaveAs("a", DocumentSaveStatus::Conflict);
    Levels.Documents["a"].SettleError = "read-only";
    EXPECT_TRUE(Prompts.Answer("take"));
    EXPECT_EQ(Problems.Count(ProblemSeverity::Error), 1u);

    Levels.Documents["a"].SettleError.clear();
    SaveAs("a", DocumentSaveStatus::Saved);
    EXPECT_EQ(Problems.Count(ProblemSeverity::Info), 0u);
}
