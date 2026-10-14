#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A local stream socket that carries newline-terminated lines, for tools on
// the same machine talking to a running editor. A Unix domain socket: its
// path decides who can reach it. Never blocks: the server is polled on its
// owner's thread, and a client that stops reading is dropped rather than
// waited on.

using LineClientId = std::uint64_t;

struct ReceivedLine
{
    LineClientId Client = 0;
    std::string  Text;
};

class LineSocketServer
{
public:
    LineSocketServer() = default;
    ~LineSocketServer();
    LineSocketServer(const LineSocketServer&) = delete;
    LineSocketServer& operator=(const LineSocketServer&) = delete;

    // Listens at `path`, replacing a socket file nobody answers on. False,
    // with `error` set, when the path is in use or cannot be bound.
    [[nodiscard]] bool Open(const std::filesystem::path& path, std::string& error);
    void Close();
    [[nodiscard]] bool IsOpen() const { return Listener >= 0; }
    [[nodiscard]] const std::filesystem::path& Path() const { return Bound; }

    // Takes new clients, sends what is waiting to go, and returns every whole
    // line that arrived, in arrival order per client.
    [[nodiscard]] std::vector<ReceivedLine> Poll();

    // Queues `line` (a newline is added) and sends as much as the client takes.
    void Send(LineClientId client, std::string_view line);
    void Broadcast(std::string_view line);
    [[nodiscard]] std::size_t ClientCount() const { return Clients.size(); }

private:
    struct Client
    {
        LineClientId Id = 0;
        int          Handle = -1;
        std::string  In;
        std::string  Out;
    };

    void Flush(Client& client);
    void Drop(std::size_t index);

    int Listener = -1;
    std::filesystem::path Bound;
    std::vector<Client> Clients;
    LineClientId NextClient = 1;
};

// The client end: one connection, read and written a line at a time.
class LineSocketClient
{
public:
    LineSocketClient() = default;
    ~LineSocketClient();
    LineSocketClient(const LineSocketClient&) = delete;
    LineSocketClient& operator=(const LineSocketClient&) = delete;

    [[nodiscard]] bool Connect(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool Send(std::string_view line);
    // The next whole line, waiting up to `timeoutMs` for it; nullopt when none
    // arrives in time or the server has gone.
    [[nodiscard]] std::optional<std::string> Receive(int timeoutMs);

private:
    int Handle = -1;
    std::string In;
};

// Where a running Kyusu listens unless told otherwise: in the user's runtime
// directory, which only they can enter, or a private one under the temp
// directory when there is none.
[[nodiscard]] std::filesystem::path DefaultEditorSocketPath();
