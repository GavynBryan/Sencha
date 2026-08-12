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

            // A pipe onto the child's stdin, which is where the console reads
            // operator commands from. It is how a dedicated server is actually
            // driven, so a test that drives one any other way is testing a path
            // nobody uses.
            int pipeFds[2] = { -1, -1 };
            if (::pipe(pipeFds) != 0)
                return;

            Pid = ::fork();
            if (Pid == 0)
            {
                // Child: content root, stdin from the pipe, then output to the
                // log both ways so a crash message is captured beside the
                // ordinary logging.
                if (::chdir(SENCHA_REPO_ROOT "/template") != 0)
                    ::_exit(127);
                ::close(pipeFds[1]);
                ::dup2(pipeFds[0], STDIN_FILENO);
                ::close(pipeFds[0]);
                const int fd = ::open(LogPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0)
                    ::_exit(127);
                ::dup2(fd, STDOUT_FILENO);
                ::dup2(fd, STDERR_FILENO);
                ::close(fd);
                ::execv(TEST_APP_PATH, raw.data());
                ::_exit(127);
            }
            ::close(pipeFds[0]);
            StdIn = pipeFds[1];
        }

        ~AppProcess()
        {
            if (StdIn >= 0)
                ::close(StdIn);
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

        [[nodiscard]] bool Started() const { return Pid > 0 && StdIn >= 0; }

        // Types a console line at the process, the way an operator would.
        bool Send(std::string line) const
        {
            line += '\n';
            std::size_t written = 0;
            while (written < line.size())
            {
                const ssize_t n = ::write(StdIn, line.data() + written,
                                          line.size() - written);
                if (n <= 0)
                    return false;
                written += static_cast<std::size_t>(n);
            }
            return true;
        }

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
        int StdIn = -1;
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

// The possession path, end to end, across two real processes.
//
// A client names an object it was given, asks the authority for it in a
// validated request, and control moves -- input routing, prediction subject,
// look control and camera, and what the authority will send it of that object's
// state. Then it hands the object back and every one of those comes off.
//
// Everything below this has focused coverage and a deterministic two-world
// test. What only two processes show is that the same path works through real
// sockets, a real handshake, a real reliable channel, and a game module loaded
// twice -- which is where the last serious defect in this area was hiding: a
// reliable message from a client was being answered with a handshake challenge
// and never delivered, and nothing but a real client sending one could see it.
TEST(HostClientProcess, AClientTakesATurretAndGivesItBack)
{
    AppProcess host({ "+map", "levels/test", "+host", "0" }, TempLogPath("host"));
    ASSERT_TRUE(host.Started());

    std::string hostLog;
    ASSERT_TRUE(host.WaitForLog("hosting on", &hostLog)) << hostLog;
    std::string port;
    ASSERT_TRUE(ParseHostPort(hostLog, port)) << hostLog;
    ASSERT_TRUE(host.WaitForLog("placed a turret", &hostLog))
        << "the host never placed a turret to take:\n" << hostLog;

    AppProcess client({ "+connect", "127.0.0.1:" + port }, TempLogPath("client"));
    ASSERT_TRUE(client.Started());

    std::string clientLog;
    ASSERT_TRUE(client.WaitForLog("predicting this player's own pawn", &clientLog))
        << clientLog;
    ASSERT_TRUE(host.WaitForLog("spawned a pawn for peer", &hostLog)) << hostLog;

    // The turret has to have arrived before it can be named, and it arrives as
    // ordinary replicated state rather than as anything the client waited for.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    ASSERT_TRUE(client.Send("turret"));
    EXPECT_TRUE(client.WaitForLog("asked the authority about turret", &clientLog))
        << "the client could not name the turret replication gave it:\n"
        << clientLog;
    EXPECT_TRUE(host.WaitForLog("took the turret", &hostLog))
        << "the request never reached the authority, or was refused:\n"
        << hostLog;

    // What the authority records, read the way an operator would read it. The
    // peer owns the turret and no longer owns the pawn it walked in on: one
    // peer drives one thing.
    ASSERT_TRUE(host.Send("net_owners"));
    EXPECT_TRUE(host.WaitForLog("owns 1", &hostLog)) << hostLog;
    EXPECT_EQ(hostLog.find("NOT REPLICATED"), std::string::npos)
        << "the peer owns something no peer can be told about:\n" << hostLog;

    // And back. The same request means leaving, because the authority holds the
    // record of who owns what and does not have to be told which was meant.
    ASSERT_TRUE(client.Send("turret"));
    EXPECT_TRUE(host.WaitForLog("left the turret", &hostLog))
        << "the client could not give the turret back:\n" << hostLog;

    int clientExit = -1;
    EXPECT_TRUE(client.StopAndWait(&clientExit));
    EXPECT_EQ(clientExit, 0);

    int hostExit = -1;
    EXPECT_TRUE(host.StopAndWait(&hostExit));
    EXPECT_EQ(hostExit, 0);
}

// The account a dedicated host can give of itself. It has no window and no
// overlay, so this is the only way to ask it anything.
TEST(HostClientProcess, ADedicatedHostAnswersForItselfOverTheConsole)
{
    AppProcess host({ "+map", "levels/test", "+host", "0" }, TempLogPath("status"));
    ASSERT_TRUE(host.Started());

    std::string hostLog;
    ASSERT_TRUE(host.WaitForLog("hosting on", &hostLog)) << hostLog;
    std::string port;
    ASSERT_TRUE(ParseHostPort(hostLog, port)) << hostLog;

    AppProcess client({ "+connect", "127.0.0.1:" + port }, TempLogPath("statuscl"));
    ASSERT_TRUE(client.Started());
    ASSERT_TRUE(host.WaitForLog("spawned a pawn for peer", &hostLog)) << hostLog;

    ASSERT_TRUE(host.Send("net_status"));
    // Three of the questions the brief says a terminal has to answer: what the
    // traffic is, whether the budget is what is holding entities back, and what
    // each peer is costing.
    EXPECT_TRUE(host.WaitForLog("traffic", &hostLog)) << hostLog;
    EXPECT_TRUE(hostLog.find("publish") != std::string::npos) << hostLog;
    EXPECT_TRUE(hostLog.find("starved") != std::string::npos) << hostLog;

    int clientExit = -1;
    EXPECT_TRUE(client.StopAndWait(&clientExit));
    int hostExit = -1;
    EXPECT_TRUE(host.StopAndWait(&hostExit));
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
