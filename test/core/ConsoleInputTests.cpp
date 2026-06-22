#include <gtest/gtest.h>

#include <core/console/ConsoleCompletion.h>
#include <core/console/ConsoleHistory.h>
#include <core/console/ConsoleRegistry.h>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    bool Contains(const std::vector<std::string>& values, std::string_view needle)
    {
        return std::find(values.begin(), values.end(), needle) != values.end();
    }

    ConsoleCommandMetadata NoopCommand(std::string name)
    {
        return ConsoleCommandMetadata{
            .Name = std::move(name),
            .Callback = [](ConsoleExecutionContext&, std::span<const std::string>) {
                return ConsoleResult{};
            },
        };
    }

    ConsoleRegistry MakeRegistry()
    {
        ConsoleRegistry reg;
        reg.RegisterCommand(NoopCommand("get"));
        reg.RegisterCommand(NoopCommand("set"));
        reg.RegisterCommand(NoopCommand("toggle"));
        reg.RegisterCommand(NoopCommand("reset"));
        reg.RegisterCVar({ .Name = "test.alpha", .Type = CVarType::Bool,
                           .DefaultValue = false, .CurrentValue = false });
        reg.RegisterCVar({ .Name = "test.beta", .Type = CVarType::Bool,
                           .DefaultValue = false, .CurrentValue = false });
        reg.RegisterCVar({ .Name = "render.width", .Type = CVarType::Int,
                           .DefaultValue = std::int64_t{0}, .CurrentValue = std::int64_t{0} });
        return reg;
    }
}

TEST(ConsoleHistory, EmptyHasNoRecall)
{
    ConsoleHistory history;
    EXPECT_FALSE(history.Prev().has_value());
    EXPECT_FALSE(history.Next().has_value());
    EXPECT_EQ(history.Size(), 0u);
}

TEST(ConsoleHistory, RecallNewestFirstAndBackToDraft)
{
    ConsoleHistory history;
    history.Push("a");
    history.Push("b");
    history.Push("c");
    ASSERT_EQ(history.Size(), 3u);

    EXPECT_EQ(history.Prev().value(), "c");
    EXPECT_EQ(history.Prev().value(), "b");
    EXPECT_EQ(history.Prev().value(), "a");
    EXPECT_EQ(history.Prev().value(), "a"); // stays at the oldest entry

    EXPECT_EQ(history.Next().value(), "b");
    EXPECT_EQ(history.Next().value(), "c");
    EXPECT_FALSE(history.Next().has_value()); // back to the draft line
    EXPECT_FALSE(history.Next().has_value()); // stays at the draft line
}

TEST(ConsoleHistory, IgnoresBlankAndConsecutiveDuplicates)
{
    ConsoleHistory history;
    history.Push("");
    history.Push("a");
    history.Push("a");
    history.Push("b");
    history.Push("b");
    history.Push("a");
    EXPECT_EQ(history.Size(), 3u); // a, b, a

    EXPECT_EQ(history.Prev().value(), "a");
    EXPECT_EQ(history.Prev().value(), "b");
    EXPECT_EQ(history.Prev().value(), "a");
}

TEST(ConsoleHistory, EvictsOldestBeyondCapacity)
{
    ConsoleHistory history(2);
    history.Push("a");
    history.Push("b");
    history.Push("c");
    EXPECT_EQ(history.Size(), 2u);

    EXPECT_EQ(history.Prev().value(), "c");
    EXPECT_EQ(history.Prev().value(), "b");
    EXPECT_EQ(history.Prev().value(), "b"); // "a" was evicted
}

TEST(ConsoleHistory, PushAndResetReseatCursor)
{
    ConsoleHistory history;
    history.Push("a");
    history.Push("b");
    EXPECT_EQ(history.Prev().value(), "b");
    EXPECT_EQ(history.Prev().value(), "a");

    history.ResetCursor();
    EXPECT_EQ(history.Prev().value(), "b"); // restarts from the newest

    EXPECT_EQ(history.Prev().value(), "a");
    history.Push("c"); // a push also reseats the cursor
    EXPECT_EQ(history.Prev().value(), "c");
}

TEST(ConsoleCompletion, FirstTokenSuggestsCommands)
{
    ConsoleRegistry reg = MakeRegistry();

    const std::vector<std::string> all = SuggestConsoleCompletions(reg, "");
    EXPECT_TRUE(Contains(all, "get"));
    EXPECT_TRUE(Contains(all, "set"));
    EXPECT_TRUE(Contains(all, "toggle"));
    EXPECT_FALSE(Contains(all, "test.alpha")); // no cvars at the command position

    const std::vector<std::string> prefixed = SuggestConsoleCompletions(reg, "to");
    EXPECT_TRUE(Contains(prefixed, "toggle"));
    EXPECT_FALSE(Contains(prefixed, "set"));
}

TEST(ConsoleCompletion, ArgumentTokenSuggestsCVars)
{
    ConsoleRegistry reg = MakeRegistry();

    const std::vector<std::string> afterSet = SuggestConsoleCompletions(reg, "set ");
    EXPECT_TRUE(Contains(afterSet, "test.alpha"));
    EXPECT_TRUE(Contains(afterSet, "test.beta"));
    EXPECT_TRUE(Contains(afterSet, "render.width"));
    EXPECT_FALSE(Contains(afterSet, "set")); // no commands at the argument position

    const std::vector<std::string> filtered = SuggestConsoleCompletions(reg, "set test.al");
    EXPECT_TRUE(Contains(filtered, "test.alpha"));
    EXPECT_FALSE(Contains(filtered, "test.beta"));
    EXPECT_FALSE(Contains(filtered, "render.width"));
}

TEST(ConsoleCompletion, ValuePositionHasNoSuggestions)
{
    ConsoleRegistry reg = MakeRegistry();
    EXPECT_TRUE(SuggestConsoleCompletions(reg, "set test.alpha ").empty());
    EXPECT_TRUE(SuggestConsoleCompletions(reg, "set test.alpha 1").empty());
}
