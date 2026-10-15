// The local line socket: whole lines in arrival order, replies to the client
// that asked, broadcasts to all, and no blocking on anyone.

#include "ipc/LineSocket.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
class LineSocketTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Root = std::filesystem::temp_directory_path()
            / ("sencha_sock_" + std::to_string(reinterpret_cast<std::uintptr_t>(this) & 0xffffff));
        std::filesystem::remove_all(Root);
        std::string error;
        ASSERT_TRUE(Server.Open(Root / "kyusu.sock", error)) << error;
    }

    void TearDown() override
    {
        Server.Close();
        std::error_code ignored;
        std::filesystem::remove_all(Root, ignored);
    }

    // A local socket has a sent line queued before send returns, so a few
    // polls take everything in flight.
    std::vector<ReceivedLine> Await(std::size_t count)
    {
        std::vector<ReceivedLine> lines;
        for (int attempt = 0; attempt < 8 && lines.size() < count; ++attempt)
            for (ReceivedLine& line : Server.Poll())
                lines.push_back(std::move(line));
        return lines;
    }

    std::filesystem::path Root;
    LineSocketServer Server;
};
}

TEST_F(LineSocketTest, LinesArriveWholeAndRepliesGoToTheClientThatAsked)
{
    LineSocketClient first, second;
    std::string error;
    ASSERT_TRUE(first.Connect(Root / "kyusu.sock", error)) << error;
    ASSERT_TRUE(second.Connect(Root / "kyusu.sock", error)) << error;
    ASSERT_TRUE(first.Send("level.summary\r"));
    ASSERT_TRUE(second.Send("deps subject=@Hall"));

    const std::vector<ReceivedLine> lines = Await(2);
    ASSERT_EQ(lines.size(), 2u);
    const ReceivedLine& fromFirst = lines[0].Text == "level.summary" ? lines[0] : lines[1];
    const ReceivedLine& fromSecond = lines[0].Text == "level.summary" ? lines[1] : lines[0];
    EXPECT_EQ(fromFirst.Text, "level.summary") << "a carriage return is not part of the line";
    EXPECT_EQ(fromSecond.Text, "deps subject=@Hall");

    Server.Send(fromSecond.Client, "for the second");
    Server.Broadcast("for everyone");
    EXPECT_EQ(second.Receive(500), "for the second");
    EXPECT_EQ(second.Receive(500), "for everyone");
    EXPECT_EQ(first.Receive(500), "for everyone");
    EXPECT_EQ(first.Receive(20), std::nullopt) << "the reply went to the client that asked only";
}

TEST_F(LineSocketTest, AClientThatLeavesIsDropped)
{
    {
        LineSocketClient leaving;
        std::string error;
        ASSERT_TRUE(leaving.Connect(Root / "kyusu.sock", error)) << error;
        ASSERT_TRUE(leaving.Send("hello"));
        ASSERT_EQ(Await(1).size(), 1u);
        EXPECT_EQ(Server.ClientCount(), 1u);
    }
    (void)Server.Poll();
    EXPECT_EQ(Server.ClientCount(), 0u);
}

TEST_F(LineSocketTest, ARunningServersPathIsNotTakenButADeadOnesIs)
{
    LineSocketServer rival;
    std::string error;
    EXPECT_FALSE(rival.Open(Root / "kyusu.sock", error));
    EXPECT_NE(error.find("another editor"), std::string::npos) << error;

    Server.Close();
    // A file nobody answers on, as a crashed editor leaves its socket.
    std::ofstream(Root / "kyusu.sock") << "";
    EXPECT_TRUE(rival.Open(Root / "kyusu.sock", error)) << error;
}

TEST_F(LineSocketTest, NothingListeningIsSaidPlainly)
{
    LineSocketClient client;
    std::string error;
    EXPECT_FALSE(client.Connect(Root / "nobody.sock", error));
    EXPECT_NE(error.find("nothing is listening"), std::string::npos);
}
