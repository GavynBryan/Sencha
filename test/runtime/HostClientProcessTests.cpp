#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

//=============================================================================
// A dedicated host and a client, as two real processes over loopback UDP.
//
// This is the shape the product ships: `app --headless` hosting a map with
// nobody playing in it, and separate processes joining. Everything below the
// process boundary already has focused coverage; what only two processes can
// show is that a host with no window, no graphics, and no local player still
// loads a world, admits a peer, serves it a pawn, and lets go cleanly when it
// is told to stop.
//
// Two processes rather than two engines in one, deliberately: the component
// serializer registry is process-global, and a second engine tearing down
// inside a live one is a known hazard the networking plan records.
//=============================================================================

#if defined(_WIN32) || !defined(TEST_APP_PATH) || !defined(TEST_TEMPLATE_MODULE_PATH)

TEST(HostClientProcess, RequiresPosixAndTheTemplateModule)
{
    GTEST_SKIP() << "two-process hosting coverage is POSIX-only and needs the "
                    "app binary plus the template game module";
}

#else

namespace
{
    constexpr std::chrono::seconds kDeadline{ 45 };

    std::filesystem::path TempLogPath(std::string_view suffix)
    {
        static int counter = 0;
        std::string caseName = "unknown";
        if (const auto* info = testing::UnitTest::GetInstance()->current_test_info())
            caseName = info->name();
        const std::string name = "sencha_host_client_" + caseName + "_"
            + std::string(suffix) + "_" + std::to_string(++counter) + ".log";
        return std::filesystem::temp_directory_path() / name;
    }

    // One `app` process with its output captured. The content root is the
    // template directory, because content roots resolve against the working
    // directory exactly as they do for a shipped game.
    class AppProcess
    {
    public:
        AppProcess(std::vector<std::string> args, std::filesystem::path logPath)
            : LogPath(std::move(logPath))
        {
            std::vector<std::string> argv{
                TEST_APP_PATH, "--headless", "--game", TEST_TEMPLATE_MODULE_PATH
            };
            argv.insert(argv.end(), args.begin(), args.end());

            std::vector<char*> raw;
            raw.reserve(argv.size() + 1);
            for (std::string& arg : argv)
                raw.push_back(arg.data());
            raw.push_back(nullptr);

            Pid = ::fork();
            if (Pid == 0)
            {
                // Child: content root, then output to the log both ways so a
                // crash message is captured beside the ordinary logging.
                if (::chdir(SENCHA_REPO_ROOT "/template") != 0)
                    ::_exit(127);
                const int fd = ::open(LogPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0)
                    ::_exit(127);
                ::dup2(fd, STDOUT_FILENO);
                ::dup2(fd, STDERR_FILENO);
                ::close(fd);
                ::execv(TEST_APP_PATH, raw.data());
                ::_exit(127);
            }
        }

        ~AppProcess()
        {
            if (Pid > 0)
            {
                ::kill(Pid, SIGKILL);
                int status = 0;
                ::waitpid(Pid, &status, 0);
            }
            std::error_code ec;
            std::filesystem::remove(LogPath, ec);
        }

        AppProcess(const AppProcess&) = delete;
        AppProcess& operator=(const AppProcess&) = delete;

        [[nodiscard]] bool Started() const { return Pid > 0; }

        [[nodiscard]] std::string Log() const
        {
            std::ifstream in(LogPath);
            std::stringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }

        // Waits for `needle` to appear in the process output. Returns the log
        // as it stood, so a caller can read more out of the same snapshot.
        [[nodiscard]] bool WaitForLog(std::string_view needle, std::string* out = nullptr) const
        {
            const auto deadline = std::chrono::steady_clock::now() + kDeadline;
            while (std::chrono::steady_clock::now() < deadline)
            {
                std::string text = Log();
                if (text.find(needle) != std::string::npos)
                {
                    if (out != nullptr)
                        *out = std::move(text);
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (out != nullptr)
                *out = Log();
            return false;
        }

        // Asks the process to stop the way an operator would, and reports its
        // exit status.
        [[nodiscard]] bool StopAndWait(int* exitCode)
        {
            if (Pid <= 0)
                return false;

            ::kill(Pid, SIGTERM);

            const auto deadline = std::chrono::steady_clock::now() + kDeadline;
            while (std::chrono::steady_clock::now() < deadline)
            {
                int status = 0;
                const pid_t done = ::waitpid(Pid, &status, WNOHANG);
                if (done == Pid)
                {
                    Pid = -1;
                    if (exitCode != nullptr)
                        *exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return false;
        }

    private:
        pid_t Pid = -1;
        std::filesystem::path LogPath;
    };

    // The port an ephemeral bind actually got. Asking for port 0 is what keeps
    // this test from colliding with a real server, or with itself.
    [[nodiscard]] bool ParseHostPort(const std::string& log, std::string& port)
    {
        const std::regex pattern(R"(hosting on [^\s:]+:(\d+))");
        std::smatch match;
        if (!std::regex_search(log, match, pattern))
            return false;
        port = match[1].str();
        return true;
    }
}

TEST(HostClientProcess, ADedicatedHostServesAJoiningClient)
{
    AppProcess host({ "+map", "levels/test", "+host", "0" }, TempLogPath("host"));
    ASSERT_TRUE(host.Started());

    std::string hostLog;
    ASSERT_TRUE(host.WaitForLog("hosting on", &hostLog))
        << "host never reported a bound address:\n" << hostLog;

    std::string port;
    ASSERT_TRUE(ParseHostPort(hostLog, port)) << hostLog;

    AppProcess client({ "+connect", "127.0.0.1:" + port }, TempLogPath("client"));
    ASSERT_TRUE(client.Started());

    // The client's side of the join: admitted, then possessing and predicting
    // the pawn the authority made for it.
    std::string clientLog;
    EXPECT_TRUE(client.WaitForLog("predicting this player's own pawn", &clientLog))
        << "client never took possession:\n" << clientLog;

    // The host's side: a peer arrived and was given a pawn, with no local
    // player of its own anywhere in it.
    EXPECT_TRUE(host.WaitForLog("spawned a pawn for peer", &hostLog))
        << "host never served the peer:\n" << hostLog;
    EXPECT_EQ(hostLog.find("local player attached"), std::string::npos)
        << "a dedicated host must not provision a player of its own:\n" << hostLog;

    // Leaving is part of the contract: the host notices and releases the pawn.
    int clientExit = -1;
    EXPECT_TRUE(client.StopAndWait(&clientExit));
    EXPECT_EQ(clientExit, 0);
    EXPECT_TRUE(host.WaitForLog("removed the pawn for peer", &hostLog))
        << "host never released the departed peer's pawn:\n" << hostLog;

    int hostExit = -1;
    EXPECT_TRUE(host.StopAndWait(&hostExit)) << "host did not stop when asked";
    EXPECT_EQ(hostExit, 0);
}

// A host with no graphics services still loads the world it is simulating.
// Before render assets could be declined, every level containing a mesh failed
// to load here and left the host with no entities and no collision.
TEST(HostClientProcess, ADedicatedHostLoadsItsMap)
{
    AppProcess host({ "+map", "levels/test" }, TempLogPath("maponly"));
    ASSERT_TRUE(host.Started());

    std::string log;
    ASSERT_TRUE(host.WaitForLog("loading map", &log)) << log;

    // The load is asynchronous, so give it frames to finish and then read what
    // it reported. A zone that failed says so on the way through.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    int exitCode = -1;
    EXPECT_TRUE(host.StopAndWait(&exitCode));
    EXPECT_EQ(exitCode, 0);

    log = host.Log();
    EXPECT_EQ(log.find("scene load error"), std::string::npos)
        << "the scene did not load headless:\n" << log;
    EXPECT_EQ(log.find("failed at import"), std::string::npos) << log;
    EXPECT_EQ(log.find("failed at finalize"), std::string::npos) << log;
}

#endif
