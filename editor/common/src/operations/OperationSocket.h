#pragma once

#include "ipc/LineSocket.h"

#include <cstdint>
#include <filesystem>
#include <string>

class EditorOperations;

// The operations served over a local line socket, so a tool beside a running
// editor works in it as the console does. Each line in is a request, either
// the script form (`name key=value ...`) or a JSON object {"id", "op", "args"}
// whose answer carries the id back; each answer is one line of JSON. Every
// event the operations emit goes to every client. Polled on the owner's
// thread, so operations run where the editor's do.
class OperationSocket
{
public:
    explicit OperationSocket(EditorOperations& operations);
    ~OperationSocket();
    OperationSocket(const OperationSocket&) = delete;
    OperationSocket& operator=(const OperationSocket&) = delete;

    [[nodiscard]] bool Open(const std::filesystem::path& path, std::string& error);
    void Close() { Server.Close(); }
    [[nodiscard]] bool IsOpen() const { return Server.IsOpen(); }
    [[nodiscard]] const std::filesystem::path& Path() const { return Server.Path(); }

    // Answers every request that has arrived.
    void Poll();

private:
    EditorOperations& Operations;
    LineSocketServer Server;
    std::uint64_t Listener = 0;
};
