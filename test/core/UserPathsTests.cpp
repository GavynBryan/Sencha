#include <gtest/gtest.h>

#include <platform/UserPaths.h>

#include <cstdlib>
#include <optional>
#include <string>

// One owner for "where does a user's configuration live", and the precedence
// it resolves. Both the console's settings archive and the launcher's project
// catalog build on it, so this is where that rule is pinned once.

namespace
{
    // The environment is process state; a test that changes it puts it back.
    class ScopedEnv
    {
    public:
        ScopedEnv(const char* name, const char* value) : Name(name)
        {
            if (const char* old = std::getenv(name))
                Previous = old;
            if (value != nullptr)
                setenv(name, value, 1);
            else
                unsetenv(name);
        }
        ~ScopedEnv()
        {
            if (Previous.has_value())
                setenv(Name, Previous->c_str(), 1);
            else
                unsetenv(Name);
        }

    private:
        const char* Name;
        std::optional<std::string> Previous;
    };
}

TEST(UserPaths, XdgConfigHomeWinsOverHome)
{
    ScopedEnv xdg("XDG_CONFIG_HOME", "/tmp/sencha-xdg");
    ScopedEnv home("HOME", "/tmp/sencha-home");
    EXPECT_EQ(UserConfigDirectory(), std::filesystem::path("/tmp/sencha-xdg"));
}

TEST(UserPaths, HomeGetsDotConfigWhenXdgIsUnset)
{
    ScopedEnv xdg("XDG_CONFIG_HOME", nullptr);
    ScopedEnv home("HOME", "/tmp/sencha-home");
    EXPECT_EQ(UserConfigDirectory(), std::filesystem::path("/tmp/sencha-home/.config"));
}

TEST(UserPaths, AnEmptyVariableCountsAsUnset)
{
    ScopedEnv xdg("XDG_CONFIG_HOME", "");
    ScopedEnv home("HOME", "/tmp/sencha-home");
    EXPECT_EQ(UserConfigDirectory(), std::filesystem::path("/tmp/sencha-home/.config"));
}

TEST(UserPaths, NeitherFallsBackToTheWorkingDirectory)
{
    ScopedEnv xdg("XDG_CONFIG_HOME", nullptr);
    ScopedEnv home("HOME", nullptr);
    EXPECT_EQ(UserConfigDirectory(), std::filesystem::path("."));
}
