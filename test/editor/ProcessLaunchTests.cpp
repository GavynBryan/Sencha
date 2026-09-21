#include "project/ProcessLaunch.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

TEST(ProcessLaunch, EmptyAndMovedHandlesAreExited)
{
    ChildProcess empty;
    EXPECT_TRUE(empty.HasExited());
    ChildProcess moved(std::move(empty));
    EXPECT_TRUE(empty.HasExited());
    EXPECT_TRUE(moved.HasExited());
}

TEST(ProcessLaunch, FailedLaunchDoesNotReturnAChild)
{
    ChildProcess child;
    std::string error;
    EXPECT_FALSE(SpawnProcess("sencha-does-not-exist/missing-executable", {}, {}, child, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(child.HasExited());
}

TEST(ProcessLaunch, ArgumentsAndWorkingDirectorySurviveNativeLaunch)
{
    const auto root = std::filesystem::temp_directory_path()
        / ("sencha process " + std::to_string(std::random_device{}()));
    ASSERT_TRUE(std::filesystem::create_directory(root));
    struct Cleanup
    {
        std::filesystem::path Root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(Root, error); }
    } cleanup{root};
    const auto output = root / "arguments.txt";
    const std::vector<std::string> expected{ "", "two words", "literal\"quote", "trailing\\",
        "space and trailing\\", "back\\\"quote", "&not-a-shell|command", "\xE7\x8C\xAB" };
    auto args = expected;
    args.insert(args.begin(), output.string());
    ChildProcess child;
    std::string error;
    ASSERT_TRUE(SpawnProcess(TEST_PROCESS_PROBE_PATH, args, root.string(), child, &error)) << error;
    ChildProcess moved(std::move(child));
    EXPECT_TRUE(child.HasExited());
    const auto launchedPid = moved.Pid();
    EXPECT_FALSE(SpawnProcess("sencha-does-not-exist/missing-executable", {}, {}, moved, &error));
    EXPECT_EQ(moved.Pid(), launchedPid);
#if defined(_WIN32)
    HANDLE wait = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(moved.Pid()));
    if (wait)
    {
        const auto result = WaitForSingleObject(wait, 10000);
        CloseHandle(wait);
        ASSERT_EQ(result, WAIT_OBJECT_0);
    }
#elif defined(__unix__) || defined(__APPLE__)
    ASSERT_GT(waitpid(static_cast<pid_t>(moved.Pid()), nullptr, 0), 0);
#endif
    EXPECT_TRUE(moved.HasExited());
    std::ifstream input(output);
    ASSERT_TRUE(input.is_open());
    std::size_t count = 0;
    input >> count;
    ASSERT_EQ(count, expected.size());
    for (const auto& value : expected)
    {
        std::string actual;
        input >> std::quoted(actual);
        EXPECT_EQ(actual, value);
    }
    std::string workingDirectory;
    input >> std::quoted(workingDirectory);
    EXPECT_TRUE(std::filesystem::equivalent(root, workingDirectory));
}
