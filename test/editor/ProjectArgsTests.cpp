#include "project/Project.h"
#include "project/ProjectArgs.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    // Restores SENCHA_PROJECT around each test so the suite is order-independent
    // and does not inherit the invoking shell's environment.
    class ProjectArgsTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const char* existing = std::getenv("SENCHA_PROJECT");
            if (existing != nullptr)
                Saved = existing;
            unsetenv("SENCHA_PROJECT");
        }

        void TearDown() override
        {
            if (Saved)
                setenv("SENCHA_PROJECT", Saved->c_str(), 1);
            else
                unsetenv("SENCHA_PROJECT");
        }

        static std::optional<std::string> Resolve(std::vector<const char*> args)
        {
            args.insert(args.begin(), "editor");
            return ResolveProjectPath(static_cast<int>(args.size()),
                                      const_cast<char**>(args.data()));
        }

        std::optional<std::string> Saved;
    };
}

TEST_F(ProjectArgsTest, NoSourcesYieldsEmpty)
{
    EXPECT_FALSE(Resolve({}).has_value());
}

TEST_F(ProjectArgsTest, ProjectFlagResolves)
{
    const auto path = Resolve({"--project", "/tmp/demo/project.senchaproj"});
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, "/tmp/demo/project.senchaproj");
}

TEST_F(ProjectArgsTest, FlagWithoutValueIsIgnored)
{
    EXPECT_FALSE(Resolve({"--project"}).has_value());
}

TEST_F(ProjectArgsTest, EnvironmentIsFallback)
{
    setenv("SENCHA_PROJECT", "/tmp/env/project.senchaproj", 1);
    const auto path = Resolve({});
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, "/tmp/env/project.senchaproj");
}

TEST_F(ProjectArgsTest, FlagWinsOverEnvironment)
{
    setenv("SENCHA_PROJECT", "/tmp/env/project.senchaproj", 1);
    const auto path = Resolve({"--project", "/tmp/flag/project.senchaproj"});
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, "/tmp/flag/project.senchaproj");
}

TEST_F(ProjectArgsTest, FlagCoexistsWithConsoleCommands)
{
    const auto path = Resolve({"+map", "levels/demo", "--project", "/tmp/p.senchaproj"});
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, "/tmp/p.senchaproj");
}

namespace
{
    std::filesystem::path MakeTempProjectDir(const char* name)
    {
        const auto dir = std::filesystem::temp_directory_path() / "sencha_project_tests" / name;
        std::filesystem::remove_all(dir);
        return dir;
    }
}

TEST(ProjectDescriptorTest, CreateThenLoadRoundTrips)
{
    const auto dir = MakeTempProjectDir("roundtrip");

    ProjectDescriptor created;
    std::string error;
    ASSERT_TRUE(ProjectDescriptor::Create(dir.string(), "Demo", created, &error)) << error;

    ProjectDescriptor loaded;
    const auto descriptorPath = dir / "project.senchaproj";
    ASSERT_TRUE(ProjectDescriptor::Load(descriptorPath.string(), loaded, &error)) << error;

    EXPECT_EQ(loaded.Name, "Demo");
    EXPECT_EQ(loaded.GameModulePath, created.GameModulePath);
    EXPECT_EQ(loaded.ContentRoots, created.ContentRoots);
    ASSERT_EQ(loaded.ContentRoots.size(), 1u);
    EXPECT_TRUE(std::filesystem::exists(loaded.ContentRoots[0]));

    std::filesystem::remove_all(dir);
}

TEST(ProjectDescriptorTest, ProjectFolderIsRelocatable)
{
    const auto original = MakeTempProjectDir("relocate_a");
    const auto moved = MakeTempProjectDir("relocate_b");

    ProjectDescriptor created;
    std::string error;
    ASSERT_TRUE(ProjectDescriptor::Create(original.string(), "Roaming", created, &error)) << error;

    std::filesystem::rename(original, moved);

    ProjectDescriptor loaded;
    const auto descriptorPath = moved / "project.senchaproj";
    ASSERT_TRUE(ProjectDescriptor::Load(descriptorPath.string(), loaded, &error)) << error;

    // Relative paths in the file resolve against the file's new directory.
    ASSERT_EQ(loaded.ContentRoots.size(), 1u);
    EXPECT_EQ(loaded.ContentRoots[0],
              (moved / "assets").lexically_normal().string());

    std::filesystem::remove_all(moved);
}
