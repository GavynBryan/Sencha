// The journal of guesses a client makes ahead of the authority: bounded, and each
// guess decided once the authority has run its command and not before.

#include <prediction/PredictionJournal.h>

#include <gtest/gtest.h>

#include <vector>

TEST(PredictionJournal, AGuessIsDecidedOnlyOnceItsCommandHasRun)
{
    PredictionJournal<int> journal(4);
    ASSERT_TRUE(journal.Add(10, 10));
    ASSERT_TRUE(journal.Add(12, 12));
    std::vector<int> decided;
    std::vector<int> undecided;
    journal.Reconcile(
        11, [&](int guess) { decided.push_back(guess); return false; },
        [&](int guess) { undecided.push_back(guess); });
    EXPECT_EQ(decided, std::vector<int>{ 10 });
    EXPECT_EQ(undecided, std::vector<int>{ 12 });
    EXPECT_EQ(journal.Size(), 1u) << "a decided guess is dropped";
}

TEST(PredictionJournal, ADecidedGuessMayBeKept)
{
    PredictionJournal<int> journal(4);
    ASSERT_TRUE(journal.Add(1, 1));
    journal.Reconcile(5, [](int) { return true; }, [](int) {});
    EXPECT_EQ(journal.Size(), 1u) << "kept while its owner still needs it";
}

TEST(PredictionJournal, PastItsCapacityNothingMoreIsGuessed)
{
    PredictionJournal<int> journal(2);
    ASSERT_TRUE(journal.Add(1, 1));
    ASSERT_TRUE(journal.Add(2, 2));
    EXPECT_TRUE(journal.Full());
    EXPECT_FALSE(journal.Add(3, 3));
    EXPECT_NE(journal.Find([](int guess) { return guess == 2; }), nullptr);
    EXPECT_EQ(journal.Find([](int guess) { return guess == 3; }), nullptr);
}
