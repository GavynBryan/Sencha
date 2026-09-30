#include "ProjectRelaunch.h"

#include <gtest/gtest.h>

#include <filesystem>

// Opening a project starts the application again beside itself, with the
// project named absolutely so the new process does not depend on this one's
// working directory.
TEST(ProjectRelaunch, NamesTheApplicationBesideThisOneAndTheProjectAbsolutely)
{
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "sdk" / "bin";
    const ProcessCommand command = BuildProjectRelaunch(base, "kyusu", "games/demo/../demo/project.senchaproj");

    std::filesystem::path expected = base / "kyusu";
#if defined(_WIN32)
    expected += ".exe";
#endif
    EXPECT_EQ(command.Binary, expected.string());
    ASSERT_EQ(command.Args.size(), 2u);
    EXPECT_EQ(command.Args[0], "--project");
    const std::filesystem::path project(command.Args[1]);
    EXPECT_TRUE(project.is_absolute());
    EXPECT_EQ(project, std::filesystem::absolute("games/demo/project.senchaproj").lexically_normal());
}
