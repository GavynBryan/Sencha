#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

//=============================================================================
// NetAddress
//
// A transport endpoint as a plain value: family, address bytes, port. Held this
// way rather than as a platform sockaddr so the protocol, session, and test
// layers can name an endpoint without any of them including a socket header.
// The one place that converts to and from platform types is the UDP transport.
//
// Comparison is by value, which is what per-peer bookkeeping keys on. Two
// addresses that differ only in port are different peers: several clients
// behind one NAT share an address and not a port.
//=============================================================================
enum class NetAddressFamily : std::uint8_t
{
    Unspecified,
    IPv4,
    IPv6,
};

struct NetAddress
{
    // IPv4 uses the first four bytes; IPv6 uses all sixteen, network order.
    std::array<std::uint8_t, 16> Bytes{};
    std::uint16_t Port = 0;
    NetAddressFamily Family = NetAddressFamily::Unspecified;

    [[nodiscard]] bool IsValid() const
    {
        return Family != NetAddressFamily::Unspecified;
    }

    bool operator==(const NetAddress&) const = default;
};

// "127.0.0.1:27500" and "[::1]:27500". The bracket form is required for IPv6
// because a bare colon-separated address is ambiguous with the port separator.
// A missing or out-of-range port, or an address that does not parse, returns
// nullopt rather than a partially filled value.
[[nodiscard]] std::optional<NetAddress> NetAddressFromString(std::string_view text);

// Round-trips through NetAddressFromString for any valid address.
[[nodiscard]] std::string NetAddressToString(const NetAddress& address);

// The loopback address of the given family, for a host and client in one
// machine (the two-instance session, and every in-process test).
[[nodiscard]] NetAddress NetAddressLoopback(std::uint16_t port,
                                            NetAddressFamily family = NetAddressFamily::IPv4);

// std::hash so an address can key a per-peer table without every caller
// writing the same combiner.
template <>
struct std::hash<NetAddress>
{
    [[nodiscard]] std::size_t operator()(const NetAddress& address) const noexcept;
};
