#include "ipc/LineSocket.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <afunix.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace
{
    // A client whose unsent lines pass this is not reading; it is dropped.
    constexpr std::size_t kMaxPending = 4u << 20;

#if defined(_WIN32)
    void CloseHandle(int handle) { ::closesocket(static_cast<SOCKET>(handle)); }
    bool MakeNonBlocking(int handle)
    {
        u_long on = 1;
        return ::ioctlsocket(static_cast<SOCKET>(handle), FIONBIO, &on) == 0;
    }
    bool WouldBlock() { return ::WSAGetLastError() == WSAEWOULDBLOCK; }
    constexpr int kNoSignal = 0;
#else
    void CloseHandle(int handle) { ::close(handle); }
    bool MakeNonBlocking(int handle)
    {
        const int flags = ::fcntl(handle, F_GETFL, 0);
        return flags >= 0 && ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
    }
    bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#if defined(MSG_NOSIGNAL)
    constexpr int kNoSignal = MSG_NOSIGNAL;
#else
    constexpr int kNoSignal = 0;
#endif
#endif

    bool AddressOf(const std::filesystem::path& path, sockaddr_un& address, std::string& error)
    {
        const std::string text = path.string();
        std::memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        if (text.size() >= sizeof(address.sun_path))
        {
            error = "the socket path is too long: " + text;
            return false;
        }
        std::memcpy(address.sun_path, text.c_str(), text.size() + 1);
        return true;
    }

    // Moves every whole line out of `buffer`, carriage returns dropped.
    template <typename Emit>
    void TakeLines(std::string& buffer, Emit&& emit)
    {
        std::size_t start = 0;
        for (std::size_t end = buffer.find('\n'); end != std::string::npos; end = buffer.find('\n', start))
        {
            std::string line = buffer.substr(start, end - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            emit(std::move(line));
            start = end + 1;
        }
        buffer.erase(0, start);
    }
}

LineSocketServer::~LineSocketServer()
{
    Close();
}

bool LineSocketServer::Open(const std::filesystem::path& path, std::string& error)
{
    Close();
    sockaddr_un address{};
    if (!AddressOf(path, address, error))
        return false;
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
#if !defined(_WIN32)
    // The directory is what keeps other users out, so only its owner may enter.
    ::chmod(path.parent_path().c_str(), S_IRWXU);
#endif

    // A socket file left by an editor that died answers nobody; one that
    // answers belongs to a running editor and is not taken from it.
    if (std::filesystem::exists(path, ignored))
    {
        LineSocketClient probe;
        std::string unused;
        if (probe.Connect(path, unused))
        {
            error = "another editor is listening at " + path.string();
            return false;
        }
        std::filesystem::remove(path, ignored);
    }

    const int handle = static_cast<int>(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (handle < 0)
    {
        error = "could not make a socket";
        return false;
    }
    if (::bind(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
        || ::listen(handle, 8) != 0 || !MakeNonBlocking(handle))
    {
        error = "could not listen at " + path.string() + ": " + std::strerror(errno);
        CloseHandle(handle);
        return false;
    }
#if !defined(_WIN32)
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
    Listener = handle;
    Bound = path;
    return true;
}

void LineSocketServer::Close()
{
    for (const Client& client : Clients)
        CloseHandle(client.Handle);
    Clients.clear();
    if (Listener >= 0)
    {
        CloseHandle(Listener);
        Listener = -1;
        std::error_code ignored;
        std::filesystem::remove(Bound, ignored);
    }
    Bound.clear();
}

std::vector<ReceivedLine> LineSocketServer::Poll()
{
    std::vector<ReceivedLine> lines;
    if (Listener < 0)
        return lines;
    for (int accepted = static_cast<int>(::accept(Listener, nullptr, nullptr)); accepted >= 0;
         accepted = static_cast<int>(::accept(Listener, nullptr, nullptr)))
    {
        if (!MakeNonBlocking(accepted))
        {
            CloseHandle(accepted);
            continue;
        }
        Clients.push_back({ NextClient++, accepted, {}, {} });
    }

    for (std::size_t index = Clients.size(); index-- > 0;)
    {
        Client& client = Clients[index];
        char chunk[4096];
        bool gone = false;
        while (true)
        {
            const auto received = ::recv(client.Handle, chunk, sizeof(chunk), 0);
            if (received > 0)
            {
                client.In.append(chunk, static_cast<std::size_t>(received));
                continue;
            }
            gone = received == 0 || !WouldBlock();
            break;
        }
        std::vector<ReceivedLine> mine;
        TakeLines(client.In, [&](std::string line) { mine.push_back({ client.Id, std::move(line) }); });
        lines.insert(lines.begin(), std::make_move_iterator(mine.begin()), std::make_move_iterator(mine.end()));
        if (gone)
        {
            Drop(index);
            continue;
        }
        Flush(client);
        if (client.Out.size() > kMaxPending)
            Drop(index);
    }
    return lines;
}

void LineSocketServer::Send(LineClientId id, std::string_view line)
{
    for (std::size_t index = 0; index < Clients.size(); ++index)
    {
        Client& client = Clients[index];
        if (client.Id != id)
            continue;
        client.Out.append(line);
        client.Out.push_back('\n');
        Flush(client);
        if (client.Out.size() > kMaxPending)
            Drop(index);
        return;
    }
}

void LineSocketServer::Broadcast(std::string_view line)
{
    std::vector<LineClientId> ids;
    for (const Client& client : Clients)
        ids.push_back(client.Id);
    for (const LineClientId id : ids)
        Send(id, line);
}

void LineSocketServer::Flush(Client& client)
{
    while (!client.Out.empty())
    {
        const auto sent = ::send(client.Handle, client.Out.data(), client.Out.size(), kNoSignal);
        if (sent <= 0)
            return;
        client.Out.erase(0, static_cast<std::size_t>(sent));
    }
}

void LineSocketServer::Drop(std::size_t index)
{
    CloseHandle(Clients[index].Handle);
    Clients.erase(Clients.begin() + static_cast<std::ptrdiff_t>(index));
}

LineSocketClient::~LineSocketClient()
{
    if (Handle >= 0)
        CloseHandle(Handle);
}

bool LineSocketClient::Connect(const std::filesystem::path& path, std::string& error)
{
    sockaddr_un address{};
    if (!AddressOf(path, address, error))
        return false;
    const int handle = static_cast<int>(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (handle < 0)
    {
        error = "could not make a socket";
        return false;
    }
    if (::connect(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        error = "nothing is listening at " + path.string();
        CloseHandle(handle);
        return false;
    }
    Handle = handle;
    return true;
}

bool LineSocketClient::Send(std::string_view line)
{
    std::string framed(line);
    framed.push_back('\n');
    std::size_t done = 0;
    while (done < framed.size())
    {
        const auto sent = ::send(Handle, framed.data() + done, framed.size() - done, kNoSignal);
        if (sent <= 0)
            return false;
        done += static_cast<std::size_t>(sent);
    }
    return true;
}

std::optional<std::string> LineSocketClient::Receive(int timeoutMs)
{
    while (true)
    {
        if (const std::size_t end = In.find('\n'); end != std::string::npos)
        {
            std::string line = In.substr(0, end);
            In.erase(0, end + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return line;
        }
#if defined(_WIN32)
        WSAPOLLFD waiting{ static_cast<SOCKET>(Handle), POLLIN, 0 };
        if (::WSAPoll(&waiting, 1, timeoutMs) <= 0)
            return std::nullopt;
#else
        pollfd waiting{ Handle, POLLIN, 0 };
        if (::poll(&waiting, 1, timeoutMs) <= 0)
            return std::nullopt;
#endif
        char chunk[4096];
        const auto received = ::recv(Handle, chunk, sizeof(chunk), 0);
        if (received <= 0)
            return std::nullopt;
        In.append(chunk, static_cast<std::size_t>(received));
    }
}

std::filesystem::path DefaultEditorSocketPath()
{
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime != nullptr && *runtime != '\0')
        return std::filesystem::path(runtime) / "sencha" / "kyusu.sock";
    std::string user = "user";
#if !defined(_WIN32)
    user = std::to_string(::getuid());
#endif
    return std::filesystem::temp_directory_path() / ("sencha-" + user) / "kyusu.sock";
}
