#include <net/UdpTransport.h>

#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
#if defined(_WIN32)
    int CloseSocket(int handle) { return ::closesocket(static_cast<SOCKET>(handle)); }
    bool WouldBlock() { return ::WSAGetLastError() == WSAEWOULDBLOCK; }
    // Winsock reports an oversized datagram as an error after discarding it.
    bool MessageTooLong() { return ::WSAGetLastError() == WSAEMSGSIZE; }
    constexpr int kReceiveFlags = 0;
#else
    int CloseSocket(int handle) { return ::close(handle); }
    bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
    bool MessageTooLong() { return false; }
#if defined(__linux__)
    // MSG_TRUNC makes recvfrom report a datagram's true length even when it was
    // cut to the buffer, which is what lets an oversized one be counted and
    // refused rather than handed up silently truncated.
    constexpr int kReceiveFlags = MSG_TRUNC;
#else
    constexpr int kReceiveFlags = 0;
#endif
#endif

    // Both directions of the one conversion between the engine's address value
    // and the platform's. Confined here so NetAddress stays socket-free.
    bool ToSockAddr(const NetAddress& address, sockaddr_storage& out, socklen_t& length)
    {
        std::memset(&out, 0, sizeof(out));
        if (address.Family == NetAddressFamily::IPv4)
        {
            auto* v4 = reinterpret_cast<sockaddr_in*>(&out);
            v4->sin_family = AF_INET;
            v4->sin_port = htons(address.Port);
            std::memcpy(&v4->sin_addr, address.Bytes.data(), 4);
            length = sizeof(sockaddr_in);
            return true;
        }
        if (address.Family == NetAddressFamily::IPv6)
        {
            auto* v6 = reinterpret_cast<sockaddr_in6*>(&out);
            v6->sin6_family = AF_INET6;
            v6->sin6_port = htons(address.Port);
            std::memcpy(&v6->sin6_addr, address.Bytes.data(), 16);
            length = sizeof(sockaddr_in6);
            return true;
        }
        return false;
    }

    NetAddress FromSockAddr(const sockaddr_storage& storage)
    {
        NetAddress address;
        if (storage.ss_family == AF_INET)
        {
            const auto* v4 = reinterpret_cast<const sockaddr_in*>(&storage);
            address.Family = NetAddressFamily::IPv4;
            address.Port = ntohs(v4->sin_port);
            std::memcpy(address.Bytes.data(), &v4->sin_addr, 4);
        }
        else if (storage.ss_family == AF_INET6)
        {
            const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&storage);
            address.Family = NetAddressFamily::IPv6;
            address.Port = ntohs(v6->sin6_port);
            std::memcpy(address.Bytes.data(), &v6->sin6_addr, 16);
        }
        return address;
    }
}

UdpTransport::~UdpTransport()
{
    UdpTransport::Close();
}

bool UdpTransport::Open(std::uint16_t port)
{
    if (IsOpen())
        return false;

    const int handle = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (handle < 0)
        return false;

#if defined(_WIN32)
    u_long nonBlocking = 1;
    if (::ioctlsocket(static_cast<SOCKET>(handle), FIONBIO, &nonBlocking) != 0)
    {
        CloseSocket(handle);
        return false;
    }
#else
    const int flags = ::fcntl(handle, F_GETFL, 0);
    if (flags < 0 || ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        CloseSocket(handle);
        return false;
    }
#endif

    sockaddr_in bindAddress{};
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_port = htons(port);
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(handle,
               reinterpret_cast<const sockaddr*>(&bindAddress),
               sizeof(bindAddress)) != 0)
    {
        CloseSocket(handle);
        return false;
    }

    // An ephemeral bind only knows its port after the fact, and the session
    // handshake has to be able to report where it actually is.
    sockaddr_storage actual{};
    socklen_t actualLength = sizeof(actual);
    if (::getsockname(handle, reinterpret_cast<sockaddr*>(&actual), &actualLength) != 0)
    {
        CloseSocket(handle);
        return false;
    }

    Socket = handle;
    Local = FromSockAddr(actual);
    if (Local.Family == NetAddressFamily::IPv4 && Local.Bytes[0] == 0)
    {
        // INADDR_ANY reports as 0.0.0.0, which is where it listens rather than
        // where a peer reaches it. Loopback is the honest answer for the local
        // sessions this ships with; a real external address is the platform
        // service's job, not this layer's.
        Local = NetAddressLoopback(Local.Port);
    }
    Slots.resize(MaxPerReceive);
    return true;
}

void UdpTransport::Close()
{
    if (Socket >= 0)
    {
        CloseSocket(Socket);
        Socket = -1;
    }
    Local = {};
    Pendings.clear();
    Received.clear();
}

bool UdpTransport::Send(const NetAddress& to, std::span<const std::byte> payload)
{
    if (!IsOpen() || payload.size() > kNetMaxDatagramBytes)
        return false;

    sockaddr_storage destination{};
    socklen_t length = 0;
    if (!ToSockAddr(to, destination, length))
        return false;

    const auto sent = ::sendto(
        Socket,
        reinterpret_cast<const char*>(payload.data()),
        static_cast<int>(payload.size()),
        0,
        reinterpret_cast<const sockaddr*>(&destination),
        length);
    return sent >= 0 && static_cast<std::size_t>(sent) == payload.size();
}

std::span<const NetDatagram> UdpTransport::Receive()
{
    Pendings.clear();
    Received.clear();
    if (!IsOpen())
        return Received;

    if (Slots.size() < MaxPerReceive)
        Slots.resize(MaxPerReceive);

    for (std::size_t index = 0; index < MaxPerReceive; ++index)
    {
        sockaddr_storage from{};
        socklen_t fromLength = sizeof(from);
        const auto received = ::recvfrom(
            Socket,
            reinterpret_cast<char*>(Slots[index].data()),
            static_cast<int>(Slots[index].size()),
            kReceiveFlags,
            reinterpret_cast<sockaddr*>(&from),
            &fromLength);

        if (received < 0)
        {
            if (WouldBlock())
                break;
            if (MessageTooLong())
            {
                ++Oversized;
                continue;
            }
            // A refused ICMP from an earlier send surfaces here on some
            // platforms. It says nothing about the next datagram, so the pump
            // keeps draining rather than treating it as the end of the queue.
            continue;
        }

        const auto length = static_cast<std::size_t>(received);
        if (length > Slots[index].size())
        {
            // Cut to the slot bound: larger than the transport allows, so it is
            // refused rather than partially interpreted.
            ++Oversized;
            continue;
        }

        // The slot is recorded rather than inferred from position: a skipped
        // read advances the slot without producing a datagram, and pairing them
        // by index would hand out the wrong bytes from that point on.
        Pendings.push_back(Pending{
            .From = FromSockAddr(from),
            .Slot = index,
            .Length = length,
        });
    }

    Received.reserve(Pendings.size());
    for (const Pending& pending : Pendings)
    {
        Received.push_back(NetDatagram{
            .From = pending.From,
            .Payload = std::span<const std::byte>(Slots[pending.Slot].data(),
                                                  pending.Length),
        });
    }
    return Received;
}
