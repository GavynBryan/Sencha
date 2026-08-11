#include <gtest/gtest.h>

#include <core/console/ConsoleLineFeed.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <string>
#include <vector>

//=============================================================================
// Reading command lines from a descriptor without blocking the frame.
//
// This is how a dedicated host is administered: its terminal is its only
// surface. A frame cannot wait for someone to finish typing, so a partial line
// has to survive across frames, and a host with no input at all has to cost
// nothing rather than fail.
//=============================================================================

#if defined(_WIN32)

TEST(ConsoleLineFeed, IsNotAvailableOnWindows)
{
    ConsoleLineFeed feed(0);
    EXPECT_TRUE(feed.Poll().empty());
}

#else

namespace
{
    // A pipe stands in for the terminal: same descriptor semantics, and the
    // test controls exactly when bytes arrive.
    class Pipe
    {
    public:
        Pipe() { EXPECT_EQ(::pipe(Fds), 0); }
        ~Pipe()
        {
            CloseWrite();
            if (Fds[0] >= 0)
                ::close(Fds[0]);
        }

        Pipe(const Pipe&) = delete;
        Pipe& operator=(const Pipe&) = delete;

        [[nodiscard]] int ReadFd() const { return Fds[0]; }

        void Write(std::string_view text)
        {
            const ssize_t written = ::write(Fds[1], text.data(), text.size());
            EXPECT_EQ(static_cast<std::size_t>(written), text.size());
        }

        void CloseWrite()
        {
            if (Fds[1] >= 0)
            {
                ::close(Fds[1]);
                Fds[1] = -1;
            }
        }

    private:
        int Fds[2]{ -1, -1 };
    };
}

TEST(ConsoleLineFeed, HandsBackCompletedLinesInOrder)
{
    Pipe pipe;
    ConsoleLineFeed feed(pipe.ReadFd());

    pipe.Write("net.max_peers 8\nquit\n");

    const std::vector<std::string> lines = feed.Poll();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "net.max_peers 8");
    EXPECT_EQ(lines[1], "quit");
}

// A frame that arrives mid-word must not execute half a command, and must not
// wait for the rest either.
TEST(ConsoleLineFeed, APartialLineWaitsForItsNewline)
{
    Pipe pipe;
    ConsoleLineFeed feed(pipe.ReadFd());

    pipe.Write("qu");
    EXPECT_TRUE(feed.Poll().empty());

    pipe.Write("it\n");
    const std::vector<std::string> lines = feed.Poll();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "quit");
}

TEST(ConsoleLineFeed, PollingAnIdleDescriptorReturnsNothingAndStaysOpen)
{
    Pipe pipe;
    ConsoleLineFeed feed(pipe.ReadFd());

    EXPECT_TRUE(feed.Poll().empty());
    EXPECT_TRUE(feed.IsOpen()) << "nothing typed yet is not end of input";
}

// A terminal that sends CRLF should not leave a carriage return on the end of
// every command.
TEST(ConsoleLineFeed, TolerateCarriageReturns)
{
    Pipe pipe;
    ConsoleLineFeed feed(pipe.ReadFd());

    pipe.Write("quit\r\n");

    const std::vector<std::string> lines = feed.Poll();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "quit");
}

// End of input is how a host with a closed stdin, or one whose terminal went
// away, stops paying for the poll -- and it is not a failure: the host keeps
// running, it just cannot be typed at any more.
TEST(ConsoleLineFeed, EndOfInputLatchesClosed)
{
    Pipe pipe;
    ConsoleLineFeed feed(pipe.ReadFd());

    pipe.Write("quit\n");
    pipe.CloseWrite();

    const std::vector<std::string> lines = feed.Poll();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "quit") << "everything already sent is still delivered";

    EXPECT_FALSE(feed.IsOpen());
    EXPECT_TRUE(feed.Poll().empty());
}

// A line left unfinished when input ended was never a command.
TEST(ConsoleLineFeed, AnUnterminatedTailIsDiscardedAtEndOfInput)
{
    Pipe pipe;
    ConsoleLineFeed feed(pipe.ReadFd());

    pipe.Write("qui");
    pipe.CloseWrite();

    EXPECT_TRUE(feed.Poll().empty());
    EXPECT_FALSE(feed.IsOpen());
}

TEST(ConsoleLineFeed, AFeedWithNoDescriptorIsClosed)
{
    ConsoleLineFeed feed;
    EXPECT_FALSE(feed.IsOpen());
    EXPECT_TRUE(feed.Poll().empty());
}

#endif
