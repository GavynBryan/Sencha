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

    // The map these cases host. Deliberately a level the checkout carries, so
    // the cook that produces the runtime scene has an authored source: a level
    // cooked once on one machine and never committed reads as passing coverage
    // everywhere it was never cooked.
    constexpr std::string_view kHostMap = "levels/room_2";

    // Where the cook lands it. Derived from the map name rather than written
    // out again, so the two cannot drift apart.
    [[nodiscard]] std::filesystem::path CookedMapScene()
    {
        return std::filesystem::path(SENCHA_REPO_ROOT) / "template/assets/.cooked"
            / (std::string(kHostMap) + ".smap");
    }

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

    // Types a console line and waits for evidence it did something, retrying
    // while it does not.
    //
    // Absorbs exactly one transient, and only this one: a client can name an
    // object only once replication has given it one, and how many frames that
    // takes depends on what else the machine is doing. Sleeping long enough
    // instead is a guess that holds until the suite runs on a busy machine,
    // which is where this was found. Bounded, so a request that genuinely never
    // works still fails rather than hanging.
    [[nodiscard]] bool SendUntilLogged(const AppProcess& process,
                                       const std::string& line,
                                       std::string_view needle,
                                       std::string* out = nullptr)
    {
        const auto deadline = std::chrono::steady_clock::now() + kDeadline;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (!process.Send(line))
                return false;
            const auto attemptUntil =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            while (std::chrono::steady_clock::now() < attemptUntil)
            {
                std::string text = process.Log();
                if (text.find(needle) != std::string::npos)
                {
                    if (out != nullptr)
                        *out = std::move(text);
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }
        return false;
    }

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

    // Stops a process the way an operator would and expects it to go quietly.
    // Every case here ends with two or three of these, and a process that
    // refuses to stop and one that stops badly are different faults -- so both
    // are reported, and both name which process they mean.
    void ExpectCleanStop(AppProcess& process, std::string_view what)
    {
        int exitCode = -1;
        EXPECT_TRUE(process.StopAndWait(&exitCode))
            << what << " did not stop when asked";
        EXPECT_EQ(exitCode, 0) << what << " did not exit cleanly";
    }
}

// Every case here hosts a map, and a map is cooked output. The cook runs as the
// CookTemplateHostMap ctest fixture rather than from inside this binary,
// because it lives in the editor authoring library and a runtime test must not
// link that.
//
// Reported here as well as required there so a missing scene is one clear
// failure instead of five forty-five-second timeouts with nothing naming the
// cause.
class HostClientProcess : public testing::Test
{
protected:
    void SetUp() override
    {
        const std::filesystem::path scene = CookedMapScene();
        if (std::filesystem::exists(scene))
            return;
#if defined(SENCHA_TEST_HOST_MAP_COOKED)
        FAIL() << "the CookTemplateHostMap fixture did not produce " << scene;
#else
        GTEST_SKIP() << "this configuration builds no level cook, so the map "
                        "these cases host cannot be produced";
#endif
    }
};

TEST_F(HostClientProcess, ADedicatedHostServesAJoiningClient)
{
    AppProcess host({ "+map", std::string(kHostMap), "+host", "0" }, TempLogPath("host"));
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
    ExpectCleanStop(client, "the client");
    EXPECT_TRUE(host.WaitForLog("removed the pawn for peer", &hostLog))
        << "host never released the departed peer's pawn:\n" << hostLog;

    ExpectCleanStop(host, "the host");
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
TEST_F(HostClientProcess, AClientTakesATurretAndGivesItBack)
{
    AppProcess host({ "+map", std::string(kHostMap), "+host", "0" }, TempLogPath("host"));
    ASSERT_TRUE(host.Started());

    std::string hostLog;
    ASSERT_TRUE(host.WaitForLog("hosting on", &hostLog)) << hostLog;
    std::string port;
    ASSERT_TRUE(ParseHostPort(hostLog, port)) << hostLog;
    // Asked for rather than waited for: a turret is content the authority puts
    // down, not something a map load produces. Retried because the map is
    // still loading behind the port being open.
    ASSERT_TRUE(SendUntilLogged(host, "turret place", "placed a turret", &hostLog))
        << "the host never placed a turret to take:\n" << hostLog;

    AppProcess client({ "+connect", "127.0.0.1:" + port }, TempLogPath("client"));
    ASSERT_TRUE(client.Started());

    std::string clientLog;
    ASSERT_TRUE(client.WaitForLog("predicting this player's own pawn", &clientLog))
        << clientLog;
    ASSERT_TRUE(host.WaitForLog("spawned a pawn for peer", &hostLog)) << hostLog;

    // The turret has to have arrived before it can be named, and it arrives as
    // ordinary replicated state rather than as anything the client waited for.
    ASSERT_TRUE(SendUntilLogged(client, "turret",
                                "asked the authority about turret", &clientLog))
        << "the client could not name the turret replication gave it:\n"
        << clientLog;
    EXPECT_TRUE(host.WaitForLog("took the turret", &hostLog))
        << "the request never reached the authority, or was refused:\n"
        << hostLog;

    // What the authority records, read the way an operator would read it. The
    // peer owns the turret AND the body it walked in on: owning and driving are
    // separate facts, so climbing into a gun does not make somebody stop owning
    // themselves. One thing is driven; two are owned.
    ASSERT_TRUE(host.Send("net_owners"));
    EXPECT_TRUE(host.WaitForLog("owns 2", &hostLog))
        << "taking a turret took the driver's own body away from them:\n"
        << hostLog;
    EXPECT_EQ(hostLog.find("NOT REPLICATED"), std::string::npos)
        << "the peer owns something no peer can be told about:\n" << hostLog;

    // And back. The same request means leaving, because the authority holds the
    // record of who owns what and does not have to be told which was meant.
    ASSERT_TRUE(client.Send("turret"));
    EXPECT_TRUE(host.WaitForLog("left the turret", &hostLog))
        << "the client could not give the turret back:\n" << hostLog;

    ExpectCleanStop(client, "the client");
    ExpectCleanStop(host, "the host");
}

// Two players at once, as three real processes.
//
// Everything about this stack is written for many peers and, until this, every
// live exercise of it had exactly one. One peer cannot tell a per-peer floor
// from a global one, cannot show ownership being refused to somebody who does
// not hold it, and cannot show a peer leaving without the session emptying.
//
// So: two clients join, each is served its own pawn, one takes the turret and
// the other is refused it, and one leaves while the other keeps playing.
TEST_F(HostClientProcess, ADedicatedHostServesTwoClientsAtOnce)
{
    AppProcess host({ "+map", std::string(kHostMap), "+host", "0" }, TempLogPath("host2"));
    ASSERT_TRUE(host.Started());

    std::string hostLog;
    ASSERT_TRUE(host.WaitForLog("hosting on", &hostLog)) << hostLog;
    std::string port;
    ASSERT_TRUE(ParseHostPort(hostLog, port)) << hostLog;
    ASSERT_TRUE(SendUntilLogged(host, "turret place", "placed a turret", &hostLog))
        << hostLog;

    AppProcess first({ "+connect", "127.0.0.1:" + port }, TempLogPath("two-c1"));
    ASSERT_TRUE(first.Started());
    std::string firstLog;
    ASSERT_TRUE(first.WaitForLog("predicting this player's own pawn", &firstLog))
        << firstLog;

    AppProcess second({ "+connect", "127.0.0.1:" + port }, TempLogPath("two-c2"));
    ASSERT_TRUE(second.Started());
    std::string secondLog;
    ASSERT_TRUE(second.WaitForLog("predicting this player's own pawn", &secondLog))
        << secondLog;

    // Two peers, two pawns. Each client predicts its own and mirrors the other.
    ASSERT_TRUE(host.WaitForLog("spawned a pawn for peer 2", &hostLog))
        << "the second peer was never served:\n" << hostLog;
    ASSERT_TRUE(host.Send("net_owners"));
    EXPECT_TRUE(host.WaitForLog("2 owns 1", &hostLog))
        << "the authority does not record the second peer driving anything:\n"
        << hostLog;
    EXPECT_NE(hostLog.find("1 owns 1"), std::string::npos)
        << "serving a second peer disturbed the first:\n" << hostLog;

    // One turret, two claimants. The second is refused because the authority
    // reads who owns it from its own record rather than from the request.
    ASSERT_TRUE(SendUntilLogged(first, "turret",
                                "asked the authority about turret", &firstLog))
        << firstLog;
    ASSERT_TRUE(host.WaitForLog("peer 1 took the turret", &hostLog)) << hostLog;

    ASSERT_TRUE(SendUntilLogged(second, "turret",
                                "asked the authority about turret", &secondLog))
        << "the second client never managed to name the turret:\n" << secondLog;
    EXPECT_TRUE(host.WaitForLog("peer 2 sent payload kind", &hostLog))
        << "a second claim on a turret somebody else is driving was not "
           "refused:\n" << hostLog;
    EXPECT_EQ(hostLog.find("peer 2 took the turret"), std::string::npos)
        << "two peers were given the same turret:\n" << hostLog;

    // One leaves; the other keeps playing and the session does not empty.
    ExpectCleanStop(second, "the second client");
    EXPECT_TRUE(host.WaitForLog("removed the pawn for peer", &hostLog))
        << "the departed peer's pawn was left behind:\n" << hostLog;

    ASSERT_TRUE(host.Send("net_status"));
    EXPECT_TRUE(host.WaitForLog("peers      1", &hostLog))
        << "the host does not report exactly one peer after the other left:\n"
        << hostLog;

    ExpectCleanStop(first, "the first client");
    ExpectCleanStop(host, "the host");
}

// The account a dedicated host can give of itself. It has no window and no
// overlay, so this is the only way to ask it anything.
TEST_F(HostClientProcess, ADedicatedHostAnswersForItselfOverTheConsole)
{
    AppProcess host({ "+map", std::string(kHostMap), "+host", "0" }, TempLogPath("status"));
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

    ExpectCleanStop(client, "the client");
    ExpectCleanStop(host, "the host");
}

// A host with no graphics services still loads the world it is simulating.
// Before render assets could be declined, every level containing a mesh failed
// to load here and left the host with no entities and no collision.
TEST_F(HostClientProcess, ADedicatedHostLoadsItsMap)
{
    AppProcess host({ "+map", std::string(kHostMap) }, TempLogPath("maponly"));
    ASSERT_TRUE(host.Started());

    std::string log;
    ASSERT_TRUE(host.WaitForLog("loading map", &log)) << log;

    // The load is asynchronous, so give it frames to finish and then read what
    // it reported. A zone that failed says so on the way through.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    ExpectCleanStop(host, "the host");

    log = host.Log();
    EXPECT_EQ(log.find("scene load error"), std::string::npos)
        << "the scene did not load headless:\n" << log;
    EXPECT_EQ(log.find("failed at import"), std::string::npos) << log;
    EXPECT_EQ(log.find("failed at finalize"), std::string::npos) << log;
}

#endif
