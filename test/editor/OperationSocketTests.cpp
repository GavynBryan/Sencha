// Operations over the local socket: a request in the script form or as JSON,
// its answer on one line (a JSON request's id carried back), and every event
// to every client.

#include "operations/EditorOperations.h"
#include "operations/OperationSocket.h"

#include <core/json/JsonParser.h>
#include <core/metadata/DataSchemaFields.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
class OperationSocketTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Root = std::filesystem::temp_directory_path()
            / ("sencha_opsock_" + std::to_string(reinterpret_cast<std::uintptr_t>(this) & 0xffffff));
        std::filesystem::remove_all(Root);

        EditorOperationEntry echo;
        echo.Definition.Name = "test.echo";
        echo.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("word", DataFieldKind::String, {}, {}),
        });
        echo.Definition.Result = echo.Definition.Parameters;
        echo.Execute = [](const JsonValue& arguments) { return EditorOperationOutcome::Ok(arguments); };
        std::vector<EditorOperationEntry> entries;
        entries.push_back(std::move(echo));
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("test", std::move(entries), errors));

        std::string error;
        ASSERT_TRUE(Socket.Open(Root / "kyusu.sock", error)) << error;
        ASSERT_TRUE(Client.Connect(Root / "kyusu.sock", error)) << error;
    }

    void TearDown() override
    {
        Socket.Close();
        std::error_code ignored;
        std::filesystem::remove_all(Root, ignored);
    }

    JsonValue Ask(std::string_view request)
    {
        EXPECT_TRUE(Client.Send(request));
        for (int poll = 0; poll < 8; ++poll)
            Socket.Poll();
        const std::optional<std::string> line = Client.Receive(1000);
        return line ? JsonParse(*line).value_or(JsonValue{}) : JsonValue{};
    }

    std::filesystem::path Root;
    EditorOperations Operations;
    OperationSocket Socket{ Operations };
    LineSocketClient Client;
};
}

TEST_F(OperationSocketTest, AScriptLineIsAnsweredAsTheConsoleWouldAnswerIt)
{
    const JsonValue answer = Ask("test.echo word=tea");
    ASSERT_TRUE(answer.IsObject());
    EXPECT_EQ(answer.Find("status")->AsString(), "ok");
    EXPECT_EQ(answer.Find("data")->Find("word")->AsString(), "tea");
}

TEST_F(OperationSocketTest, AJsonRequestsAnswerCarriesItsIdBack)
{
    const JsonValue answer = Ask(R"({"id": 7, "op": "test.echo", "args": {"word": "kettle"}})");
    ASSERT_TRUE(answer.IsObject());
    EXPECT_EQ(answer.Find("id")->AsNumber(), 7.0);
    EXPECT_EQ(answer.Find("data")->Find("word")->AsString(), "kettle");
    EXPECT_EQ(Ask(R"({"id": "x"})").Find("code")->AsString(), "request.malformed");
}

TEST_F(OperationSocketTest, EveryClientHearsEveryEvent)
{
    LineSocketClient other;
    std::string error;
    ASSERT_TRUE(other.Connect(Root / "kyusu.sock", error)) << error;
    Socket.Poll();
    Operations.Emit(JsonValue(JsonValue::Object{ { "event", JsonValue("selection") } }));
    for (LineSocketClient* client : { &Client, &other })
    {
        const std::optional<std::string> line = client->Receive(1000);
        ASSERT_TRUE(line.has_value());
        EXPECT_EQ(JsonParse(*line)->Find("event")->AsString(), "selection");
    }
}
