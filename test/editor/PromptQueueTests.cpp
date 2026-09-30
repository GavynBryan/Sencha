#include "prompts/PromptQueue.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
PromptRequest Question(std::string title, std::vector<std::string>& answers)
{
    return { .Title = std::move(title),
             .Message = {},
             .Choices = { { "yes", "Yes" }, { "cancel", "Cancel" } },
             .Answer = [&answers](std::string_view key) { answers.emplace_back(key); } };
}
} // namespace

TEST(PromptQueue, QuestionsAreAnsweredOneAtATimeOldestFirst)
{
    PromptQueue queue;
    PromptQueue::Asker asker = queue.MakeAsker();
    std::vector<std::string> answers;
    asker.Ask(Question("first", answers));
    asker.Ask(Question("second", answers));

    ASSERT_NE(queue.Front(), nullptr);
    EXPECT_EQ(queue.Front()->Title, "first");
    EXPECT_FALSE(queue.Answer("no")) << "a key the prompt does not offer is refused";
    EXPECT_EQ(queue.Front()->Title, "first");

    EXPECT_TRUE(queue.Answer("yes"));
    EXPECT_EQ(queue.Front()->Title, "second");
    EXPECT_TRUE(queue.Dismiss()) << "dismissing answers cancel";
    EXPECT_EQ(queue.Front(), nullptr);
    EXPECT_EQ(answers, (std::vector<std::string>{ "yes", "cancel" }));
}

TEST(PromptQueue, AnAnswerThatAsksAgainWaitsBehindTheRest)
{
    PromptQueue queue;
    PromptQueue::Asker asker = queue.MakeAsker();
    std::vector<std::string> answers;
    asker.Ask({ .Title = "first",
                .Message = {},
                .Choices = { { "again", "Again" } },
                .Answer = [&](std::string_view) { asker.Ask(Question("follow-up", answers)); } });
    asker.Ask(Question("second", answers));

    EXPECT_TRUE(queue.Answer("again"));
    EXPECT_EQ(queue.Front()->Title, "second");
    EXPECT_TRUE(queue.Answer("yes"));
    EXPECT_EQ(queue.Front()->Title, "follow-up");
}

TEST(PromptQueue, AnAskerThatGoesTakesItsQuestionsUnanswered)
{
    PromptQueue queue;
    PromptQueue::Asker staying = queue.MakeAsker();
    std::vector<std::string> answers;
    {
        PromptQueue::Asker leaving = queue.MakeAsker();
        leaving.Ask(Question("leaving", answers));
        staying.Ask(Question("staying", answers));
        EXPECT_TRUE(leaving.IsAsking());
    }
    ASSERT_NE(queue.Front(), nullptr);
    EXPECT_EQ(queue.Front()->Title, "staying");
    EXPECT_TRUE(answers.empty()) << "a withdrawn question is never answered";

    PromptQueue::Asker moved = queue.MakeAsker();
    moved.Ask(Question("moved", answers));
    PromptQueue::Asker target = std::move(moved);
    EXPECT_FALSE(moved.IsAsking());
    EXPECT_TRUE(target.IsAsking()) << "moving an asker keeps its questions";
}
