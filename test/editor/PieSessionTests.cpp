#include "project/PieSession.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

// What stopping a play session reports. A stop is the runtime's ordinary exit,
// so a crash on the way out is a failure the editor has to show, not swallow.

#if defined(__unix__) || defined(__APPLE__)
namespace
{
    // A stand-in runtime: loops until SIGTERM, then does what `onTerm` says.
    class FakeRuntime
    {
    public:
        explicit FakeRuntime(const std::string& onTerm)
        {
            Dir = std::filesystem::temp_directory_path()
                / ("sencha-pie-session-"
                   + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
            std::filesystem::create_directories(Dir);
            Script = Dir / "app.sh";
            std::ofstream out(Script);
            out << "#!/bin/sh\ntrap '" << onTerm << "' TERM\n: > ready\n"
                << "while :; do sleep 0.02; done\n";
            out.close();
            std::filesystem::permissions(Script, std::filesystem::perms::owner_all);
        }
        ~FakeRuntime()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Dir, ignored);
        }

        // Returns once the script has installed its handler, so a stop never
        // races the shell's own startup.
        bool Launch(PieSession& session) const
        {
            std::string error;
            if (!session.Launch(Script.string(), "game.so", Dir.string(), {}, &error))
                return false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!std::filesystem::exists(Dir / "ready"))
            {
                if (std::chrono::steady_clock::now() > deadline)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return true;
        }

    private:
        std::filesystem::path Dir;
        std::filesystem::path Script;
    };
}

TEST(PieSession, AStopThatExitsCleanlyReportsNothing)
{
    const FakeRuntime runtime("exit 0");
    PieSession session;
    ASSERT_TRUE(runtime.Launch(session));
    session.Stop();
    EXPECT_FALSE(session.IsRunning());
    EXPECT_EQ(session.TakeExitReport(), std::nullopt);
}

TEST(PieSession, ACrashWhileStoppingIsReported)
{
    const FakeRuntime runtime("kill -USR1 $$");
    PieSession session;
    ASSERT_TRUE(runtime.Launch(session));
    session.Stop();
    const std::optional<std::string> report = session.TakeExitReport();
    ASSERT_TRUE(report.has_value());
    EXPECT_NE(report->find("signal"), std::string::npos) << *report;
}

TEST(PieSession, AFailedExitWhileStoppingIsReported)
{
    const FakeRuntime runtime("exit 3");
    PieSession session;
    ASSERT_TRUE(runtime.Launch(session));
    session.Stop();
    const std::optional<std::string> report = session.TakeExitReport();
    ASSERT_TRUE(report.has_value());
    EXPECT_NE(report->find("code 3"), std::string::npos) << *report;
}

TEST(PieSession, ARuntimeThatIgnoresTheStopIsKilledAfterTheGrace)
{
    const FakeRuntime runtime("");
    PieSession session;
    ASSERT_TRUE(runtime.Launch(session));
    const auto started = std::chrono::steady_clock::now();
    session.Stop(std::chrono::milliseconds(200));
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
    EXPECT_FALSE(session.IsRunning());
    const std::optional<std::string> report = session.TakeExitReport();
    ASSERT_TRUE(report.has_value());
    EXPECT_NE(report->find("killed"), std::string::npos) << *report;
}
#endif
