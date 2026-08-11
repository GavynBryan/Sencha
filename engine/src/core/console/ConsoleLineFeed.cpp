#include <core/console/ConsoleLineFeed.h>

#if defined(_WIN32)
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

ConsoleLineFeed::ConsoleLineFeed(int fd)
    : Descriptor(fd)
    , Open(fd >= 0)
{
}

#if defined(_WIN32)

// Windows console input is a different mechanism (a handle with its own
// waitable semantics, not a pollable descriptor). A host there is driven by its
// startup script and stopped by Ctrl-C, which the process host already handles.
std::vector<std::string> ConsoleLineFeed::Poll()
{
    Open = false;
    return {};
}

#else

std::vector<std::string> ConsoleLineFeed::Poll()
{
    std::vector<std::string> lines;
    if (!Open)
        return lines;

    for (;;)
    {
        pollfd waiting{};
        waiting.fd = Descriptor;
        waiting.events = POLLIN;

        // Zero timeout: this asks what has already arrived and returns.
        const int ready = ::poll(&waiting, 1, 0);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            Open = false;
            return lines;
        }
        if (ready == 0)
            return lines;

        // Hangup with nothing left to read is the end of input. POLLIN can be
        // set alongside it, so the read below is what decides.
        char buffer[512];
        const ssize_t got = ::read(Descriptor, buffer, sizeof(buffer));
        if (got < 0)
        {
            if (errno == EINTR)
                continue;
            // Nothing there after all -- another reader took it, or the
            // descriptor is non-blocking and empty.
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return lines;
            Open = false;
            return lines;
        }
        if (got == 0)
        {
            // End of input. A partial line with no newline behind it was never
            // finished, so it is not a command.
            Open = false;
            Partial.clear();
            return lines;
        }

        for (ssize_t i = 0; i < got; ++i)
        {
            const char c = buffer[i];
            if (c == '\n')
            {
                // Tolerate CRLF from a terminal that sends it.
                if (!Partial.empty() && Partial.back() == '\r')
                    Partial.pop_back();
                lines.push_back(Partial);
                Partial.clear();
                continue;
            }
            Partial.push_back(c);
        }
    }
}

#endif
