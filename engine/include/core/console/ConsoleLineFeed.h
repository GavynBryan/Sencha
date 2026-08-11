#pragma once

#include <string>
#include <vector>

//=============================================================================
// ConsoleLineFeed
//
// Reads whole command lines from a file descriptor without ever blocking the
// frame.
//
// A dedicated host has a console but no way to reach it: no window, no overlay,
// nothing typing into it. Its terminal is the administration surface, and this
// is the part of that which belongs to the engine -- turning bytes that have
// arrived into complete lines, one poll per frame, on the frame's own thread.
//
// Which descriptor, and whether to read one at all, is the process host's
// decision (EngineConsoleConfig::CommandFd). The feed takes a descriptor and
// asks no questions about where it came from, which is what lets a test drive
// it from a pipe and what keeps two engines in one process from quietly
// fighting over the same standard input.
//
// A closed or unreadable descriptor latches shut: a host started with its input
// redirected from nowhere polls once, learns there is nothing there, and stops
// paying for it. That is not an error -- plenty of servers run with no terminal
// at all -- so it is not reported as one.
//=============================================================================
class ConsoleLineFeed
{
public:
    ConsoleLineFeed() = default;
    explicit ConsoleLineFeed(int fd);

    ConsoleLineFeed(const ConsoleLineFeed&) = delete;
    ConsoleLineFeed& operator=(const ConsoleLineFeed&) = delete;

    // Complete lines that have arrived since the last call, in order. Never
    // blocks: a line still being typed stays buffered until its newline.
    [[nodiscard]] std::vector<std::string> Poll();

    // False once the descriptor reached end of input or could not be read, and
    // for a feed that was never given one.
    [[nodiscard]] bool IsOpen() const { return Open; }

private:
    int Descriptor = -1;
    bool Open = false;
    std::string Partial;
};
