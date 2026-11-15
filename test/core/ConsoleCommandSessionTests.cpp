#include <gtest/gtest.h>

#include <core/console/ConsoleService.h>

#include <string>

namespace
{
    ConsoleAuthorityPolicy Solo()
    {
        return ConsoleAuthorityPolicy{};
    }

    ConsoleAuthorityPolicy Host(bool cheats)
    {
        return ConsoleAuthorityPolicy{ .InSession = true, .HasAuthority = true, .CheatsEnabled = cheats };
    }

    ConsoleAuthorityPolicy Client(bool cheats)
    {
        return ConsoleAuthorityPolicy{ .InSession = true, .HasAuthority = false, .CheatsEnabled = cheats };
    }

    struct Console
    {
        ConsoleService Service;
        int Runs = 0;

        Console()
        {
            Register("test.plain", ConsoleCommandFlags::None);
            Register("test.cheat", ConsoleCommandFlags::Cheat);
            Register("test.authority", ConsoleCommandFlags::AuthorityOnly);
            Register("test.mutate", ConsoleCommandFlags::Cheat | ConsoleCommandFlags::AuthorityOnly);
        }

        void Register(std::string name, ConsoleCommandFlags flags)
        {
            ASSERT_TRUE(Service.Registry().RegisterCommand({
                .Name = std::move(name),
                .Owner = "test",
                .Flags = flags,
                .Callback = [this](ConsoleExecutionContext&, std::span<const std::string>) {
                    ++Runs;
                    return ConsoleResult{};
                },
            }));
        }

        ConsoleResult Run(std::string_view line, const ConsoleAuthorityPolicy& policy)
        {
            Service.Registry().SetAuthorityPolicy(policy);
            return Service.ExecuteLine(line);
        }
    };

    std::string FirstLine(const ConsoleResult& result)
    {
        return result.Output.empty() ? std::string() : result.Output.front().Text;
    }
}

TEST(ConsoleCommandSession, OutsideASessionEveryFlaggedCommandRuns)
{
    Console console;
    for (const char* name : { "test.plain", "test.cheat", "test.authority", "test.mutate" })
        EXPECT_TRUE(console.Run(name, Solo()).Succeeded()) << name;
    EXPECT_EQ(console.Runs, 4);
}

TEST(ConsoleCommandSession, AClientCannotRunWhatOnlyTheAuthorityMay)
{
    Console console;
    const ConsoleResult refused = console.Run("test.authority", Client(true));
    EXPECT_EQ(refused.Status, ConsoleStatus::ValidationFailed);
    EXPECT_EQ(FirstLine(refused), "test.authority runs only on the server");
    EXPECT_EQ(console.Runs, 0) << "the callback ran despite the refusal";

    EXPECT_TRUE(console.Run("test.authority", Host(false)).Succeeded());
    EXPECT_EQ(console.Runs, 1);
}

TEST(ConsoleCommandSession, ACheatIsRefusedForEveryoneUntilTheSessionAllowsCheats)
{
    Console console;
    for (const ConsoleAuthorityPolicy& policy : { Host(false), Client(false) })
    {
        const ConsoleResult refused = console.Run("test.cheat", policy);
        EXPECT_EQ(refused.Status, ConsoleStatus::ValidationFailed);
        EXPECT_EQ(FirstLine(refused), "test.cheat is a cheat and net.cheats is off");
    }
    EXPECT_EQ(console.Runs, 0);

    EXPECT_TRUE(console.Run("test.cheat", Host(true)).Succeeded());
    EXPECT_TRUE(console.Run("test.cheat", Client(true)).Succeeded());
    EXPECT_EQ(console.Runs, 2);
}

TEST(ConsoleCommandSession, ACommandCarryingBothRulesNeedsBoth)
{
    Console console;
    EXPECT_FALSE(console.Run("test.mutate", Client(true)).Succeeded());
    EXPECT_FALSE(console.Run("test.mutate", Host(false)).Succeeded());
    EXPECT_EQ(console.Runs, 0);
    EXPECT_TRUE(console.Run("test.mutate", Host(true)).Succeeded());
    EXPECT_EQ(console.Runs, 1);
}

TEST(ConsoleCommandSession, AnUnflaggedCommandRunsOnAClient)
{
    Console console;
    EXPECT_TRUE(console.Run("test.plain", Client(false)).Succeeded());
    EXPECT_EQ(console.Runs, 1);
}

TEST(ConsoleCommandSession, ADeferredCommandIsJudgedWhenItRunsNotWhenItWasQueued)
{
    Console console;
    ASSERT_TRUE(console.Service.Registry().RegisterCommand({
        .Name = "test.late",
        .Owner = "test",
        .Flags = ConsoleCommandFlags::AuthorityOnly,
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&console](ConsoleExecutionContext&, std::span<const std::string>) {
            ++console.Runs;
            return ConsoleResult{};
        },
    }));

    console.Service.Registry().SetAuthorityPolicy(Solo());
    EXPECT_EQ(console.Service.ExecuteLine("test.late", {}, true).Status, ConsoleStatus::Deferred);
    console.Service.Registry().SetAuthorityPolicy(Client(true));
    console.Service.AdvancePhase(ConsolePhase::GameLoaded);
    EXPECT_EQ(console.Runs, 0);
}
