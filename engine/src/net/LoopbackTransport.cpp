#include <net/LoopbackTransport.h>

#include <utility>

NetAddress LoopbackNetwork::Bind(LoopbackTransport& transport, std::uint16_t port)
{
    if (port != 0)
    {
        const NetAddress requested = NetAddressLoopback(port);
        if (Bound.contains(requested))
            return {};
        Bound.emplace(requested, &transport);
        return requested;
    }

    // Ephemeral: walk forward until a free port turns up. The range is the
    // usual ephemeral one so a loopback address reads like a real one in logs.
    for (std::uint32_t attempt = 0; attempt < 16384; ++attempt)
    {
        const NetAddress candidate = NetAddressLoopback(NextEphemeralPort);
        if (NextEphemeralPort == 65535)
            NextEphemeralPort = 49152;
        else
            ++NextEphemeralPort;

        if (!Bound.contains(candidate))
        {
            Bound.emplace(candidate, &transport);
            return candidate;
        }
    }
    return {};
}

void LoopbackNetwork::Unbind(const NetAddress& address)
{
    Bound.erase(address);
}

bool LoopbackNetwork::Deliver(const NetAddress& to,
                              const NetAddress& from,
                              std::span<const std::byte> payload)
{
    const auto it = Bound.find(to);
    if (it == Bound.end())
        return false;
    it->second->Accept(from, payload);
    return true;
}

LoopbackTransport::LoopbackTransport(LoopbackNetwork& network)
    : Network(network)
{
}

LoopbackTransport::~LoopbackTransport()
{
    LoopbackTransport::Close();
}

bool LoopbackTransport::Open(std::uint16_t port)
{
    if (IsOpen())
        return false;
    Local = Network.Bind(*this, port);
    return Local.IsValid();
}

void LoopbackTransport::Close()
{
    if (!Local.IsValid())
        return;
    Network.Unbind(Local);
    Local = {};
    Inbox.clear();
    Delivered.clear();
    Received.clear();
}

bool LoopbackTransport::Send(const NetAddress& to, std::span<const std::byte> payload)
{
    if (!IsOpen() || !to.IsValid())
        return false;
    // The size bound is the transport contract, not a socket detail, so the
    // in-process transport refuses exactly what the socket one would.
    if (payload.size() > kNetMaxDatagramBytes)
        return false;
    // An unrouted datagram is still a successful send: the wire does not tell
    // you nobody was listening either.
    (void)Network.Deliver(to, Local, payload);
    return true;
}

void LoopbackTransport::Accept(const NetAddress& from, std::span<const std::byte> payload)
{
    Pending pending;
    pending.From = from;
    pending.Payload.assign(payload.begin(), payload.end());
    Inbox.push_back(std::move(pending));
}

std::span<const NetDatagram> LoopbackTransport::Receive()
{
    // Swap rather than copy: the previous batch's payloads stay alive exactly
    // until this call, which is the lifetime the seam promises.
    Delivered.clear();
    Delivered.swap(Inbox);

    Received.clear();
    Received.reserve(Delivered.size());
    for (const Pending& pending : Delivered)
    {
        Received.push_back(NetDatagram{
            .From = pending.From,
            .Payload = std::span<const std::byte>(pending.Payload),
        });
    }
    return Received;
}
