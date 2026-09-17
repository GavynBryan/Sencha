#include <gtest/gtest.h>

#include <HostArguments.h>

#include <initializer_list>
#include <string>
#include <vector>

// The runtime host's command-line policy, as a function. What is asserted here
// is the precedence between a launch's posture and where it may save settings,
// and that no two flags depend on the order they were typed in -- the bug
// class this exists to pin is a server or a test host loading whatever window
// mode the developer last saved.

namespace
{
    struct Argv
    {
        std::vector<std::string> Storage;
        std::vector<char*> Pointers;
        int Count = 0;

        explicit Argv(std::initializer_list<const char*> args)
        {
            Storage.emplace_back("app");
            for (const char* arg : args)
                Storage.emplace_back(arg);
            for (std::string& s : Storage)
                Pointers.push_back(s.data());
            Pointers.push_back(nullptr);
            Count = static_cast<int>(Storage.size());
        }

        [[nodiscard]] std::vector<std::string> Remaining() const
        {
            std::vector<std::string> out;
            for (int i = 1; i < Count; ++i)
                out.emplace_back(Pointers[static_cast<std::size_t>(i)]);
            return out;
        }
    };

    constexpr const char* kPlatformRoot = "/home/somebody/.config/sencha";
}

TEST(HostArguments, ADesktopLaunchPersistsUnderThePlatformRoot)
{
    Argv argv{ "+map", "levels/foo" };
    const HostArguments args = ParseHostArguments(argv.Count, argv.Pointers.data());
    EXPECT_FALSE(args.Headless);
    EXPECT_EQ(ResolveSettingsRoot(args, kPlatformRoot), kPlatformRoot);
    EXPECT_EQ(argv.Remaining(), (std::vector<std::string>{ "+map", "levels/foo" }));
}

TEST(HostArguments, HeadlessPersistsNothingByDefault)
{
    Argv argv{ "--headless", "+map", "levels/foo" };
    const HostArguments args = ParseHostArguments(argv.Count, argv.Pointers.data());
    EXPECT_TRUE(args.Headless);
    EXPECT_TRUE(ResolveSettingsRoot(args, kPlatformRoot).empty());
}

TEST(HostArguments, SettingsNoneCutsADesktopLaunchOffFromDisk)
{
    Argv argv{ "--settings", "none" };
    const HostArguments args = ParseHostArguments(argv.Count, argv.Pointers.data());
    EXPECT_FALSE(args.Headless);
    EXPECT_TRUE(ResolveSettingsRoot(args, kPlatformRoot).empty());
}

TEST(HostArguments, AnExplicitDirectoryWinsWhateverThePosture)
{
    Argv desktop{ "--settings", "/tmp/x" };
    const HostArguments a = ParseHostArguments(desktop.Count, desktop.Pointers.data());
    EXPECT_EQ(ResolveSettingsRoot(a, kPlatformRoot), "/tmp/x");

    Argv headless{ "--headless", "--settings", "/tmp/x" };
    const HostArguments b = ParseHostArguments(headless.Count, headless.Pointers.data());
    EXPECT_TRUE(b.Headless);
    EXPECT_EQ(ResolveSettingsRoot(b, kPlatformRoot), "/tmp/x");
}

TEST(HostArguments, FlagOrderDoesNotChangeTheLaunch)
{
    Argv one{ "--headless", "--settings", "/tmp/x", "--game", "g.so", "+map", "m" };
    Argv two{ "+map", "m", "--game", "g.so", "--settings", "/tmp/x", "--headless" };
    const HostArguments a = ParseHostArguments(one.Count, one.Pointers.data());
    const HostArguments b = ParseHostArguments(two.Count, two.Pointers.data());

    EXPECT_EQ(a.Headless, b.Headless);
    EXPECT_EQ(a.GamePath, b.GamePath);
    EXPECT_EQ(a.Settings, b.Settings);
    EXPECT_EQ(ResolveSettingsRoot(a, kPlatformRoot), ResolveSettingsRoot(b, kPlatformRoot));
    EXPECT_EQ(one.Remaining(), (std::vector<std::string>{ "+map", "m" }));
    EXPECT_EQ(two.Remaining(), (std::vector<std::string>{ "+map", "m" }));
}

TEST(HostArguments, ContentRootsKeepTheirOrderAndAValuelessFlagIsLeftAlone)
{
    Argv argv{ "--content-root", "a", "--content-root", "b", "--settings" };
    const HostArguments args = ParseHostArguments(argv.Count, argv.Pointers.data());
    EXPECT_EQ(args.ContentRoots, (std::vector<std::string>{ "a", "b" }));
    // A trailing --settings with nothing after it is not a setting; the engine
    // gets to reject it as an unknown startup command rather than this eating it.
    EXPECT_FALSE(args.Settings.has_value());
    EXPECT_EQ(argv.Remaining(), (std::vector<std::string>{ "--settings" }));
}
