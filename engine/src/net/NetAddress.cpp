#include <net/NetAddress.h>

#include <charconv>
#include <cstdio>

namespace
{
    // Parses a dotted-quad into the first four address bytes. Deliberately not
    // inet_pton: this file is reachable from every net consumer, and pulling a
    // socket header in here would put platform types in the one layer that
    // exists to keep them out.
    bool ParseIPv4(std::string_view text, std::array<std::uint8_t, 16>& out)
    {
        std::size_t index = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= text.size(); ++i)
        {
            const bool atEnd = i == text.size();
            if (!atEnd && text[i] != '.')
                continue;

            const std::string_view part = text.substr(start, i - start);
            if (part.empty() || part.size() > 3 || index >= 4)
                return false;
            // Leading zeros are rejected: "010" reading as ten in one parser and
            // eight in another is a classic address-spoofing seam.
            if (part.size() > 1 && part[0] == '0')
                return false;

            unsigned value = 0;
            const auto result =
                std::from_chars(part.data(), part.data() + part.size(), value);
            if (result.ec != std::errc{} || result.ptr != part.data() + part.size())
                return false;
            if (value > 255)
                return false;

            out[index++] = static_cast<std::uint8_t>(value);
            start = i + 1;
        }
        return index == 4;
    }

    bool ParseIPv6(std::string_view text, std::array<std::uint8_t, 16>& out)
    {
        // Groups before and after the "::" elision, each a 16-bit hex field.
        std::array<std::uint16_t, 8> head{};
        std::array<std::uint16_t, 8> tail{};
        std::size_t headCount = 0;
        std::size_t tailCount = 0;
        bool seenElision = false;

        std::size_t i = 0;
        while (i < text.size())
        {
            if (text[i] == ':')
            {
                if (i + 1 < text.size() && text[i + 1] == ':')
                {
                    if (seenElision)
                        return false;
                    seenElision = true;
                    i += 2;
                    continue;
                }
                // A single colon must separate groups, never lead or trail.
                if (i == 0 || i + 1 == text.size())
                    return false;
                ++i;
                continue;
            }

            const std::size_t start = i;
            while (i < text.size() && text[i] != ':')
                ++i;
            const std::string_view part = text.substr(start, i - start);
            if (part.empty() || part.size() > 4)
                return false;

            std::uint16_t value = 0;
            const auto result =
                std::from_chars(part.data(), part.data() + part.size(), value, 16);
            if (result.ec != std::errc{} || result.ptr != part.data() + part.size())
                return false;

            if (seenElision)
            {
                if (tailCount >= tail.size())
                    return false;
                tail[tailCount++] = value;
            }
            else
            {
                if (headCount >= head.size())
                    return false;
                head[headCount++] = value;
            }
        }

        if (seenElision)
        {
            if (headCount + tailCount > 7)
                return false;
        }
        else if (headCount != 8)
        {
            return false;
        }

        out = {};
        for (std::size_t g = 0; g < headCount; ++g)
        {
            out[g * 2] = static_cast<std::uint8_t>(head[g] >> 8);
            out[g * 2 + 1] = static_cast<std::uint8_t>(head[g] & 0xFF);
        }
        const std::size_t tailStart = 8 - tailCount;
        for (std::size_t g = 0; g < tailCount; ++g)
        {
            out[(tailStart + g) * 2] = static_cast<std::uint8_t>(tail[g] >> 8);
            out[(tailStart + g) * 2 + 1] = static_cast<std::uint8_t>(tail[g] & 0xFF);
        }
        return true;
    }

    bool ParsePort(std::string_view text, std::uint16_t& out)
    {
        if (text.empty() || text.size() > 5)
            return false;
        unsigned value = 0;
        const auto result =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
            return false;
        if (value > 65535)
            return false;
        out = static_cast<std::uint16_t>(value);
        return true;
    }
}

std::optional<NetAddress> NetAddressFromString(std::string_view text)
{
    if (text.empty())
        return std::nullopt;

    NetAddress address;

    if (text.front() == '[')
    {
        const std::size_t close = text.find(']');
        if (close == std::string_view::npos)
            return std::nullopt;
        if (close + 1 >= text.size() || text[close + 1] != ':')
            return std::nullopt;
        if (!ParseIPv6(text.substr(1, close - 1), address.Bytes))
            return std::nullopt;
        if (!ParsePort(text.substr(close + 2), address.Port))
            return std::nullopt;
        address.Family = NetAddressFamily::IPv6;
        return address;
    }

    const std::size_t colon = text.rfind(':');
    if (colon == std::string_view::npos)
        return std::nullopt;
    if (!ParseIPv4(text.substr(0, colon), address.Bytes))
        return std::nullopt;
    if (!ParsePort(text.substr(colon + 1), address.Port))
        return std::nullopt;
    address.Family = NetAddressFamily::IPv4;
    return address;
}

std::string NetAddressToString(const NetAddress& address)
{
    char buffer[64];
    if (address.Family == NetAddressFamily::IPv4)
    {
        std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u:%u",
                      address.Bytes[0], address.Bytes[1],
                      address.Bytes[2], address.Bytes[3],
                      address.Port);
        return buffer;
    }
    if (address.Family == NetAddressFamily::IPv6)
    {
        // Full form, no "::" elision: this is diagnostic and log text, and an
        // address that always reads the same way is easier to grep than one
        // that shortens differently depending on where the zeros fall.
        std::snprintf(buffer, sizeof(buffer),
                      "[%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
                      "%02x%02x:%02x%02x:%02x%02x:%02x%02x]:%u",
                      address.Bytes[0], address.Bytes[1], address.Bytes[2], address.Bytes[3],
                      address.Bytes[4], address.Bytes[5], address.Bytes[6], address.Bytes[7],
                      address.Bytes[8], address.Bytes[9], address.Bytes[10], address.Bytes[11],
                      address.Bytes[12], address.Bytes[13], address.Bytes[14], address.Bytes[15],
                      address.Port);
        return buffer;
    }
    return "<unspecified>";
}

NetAddress NetAddressLoopback(std::uint16_t port, NetAddressFamily family)
{
    NetAddress address;
    address.Family = family;
    address.Port = port;
    if (family == NetAddressFamily::IPv4)
    {
        address.Bytes[0] = 127;
        address.Bytes[3] = 1;
    }
    else if (family == NetAddressFamily::IPv6)
    {
        address.Bytes[15] = 1;
    }
    return address;
}

std::size_t std::hash<NetAddress>::operator()(const NetAddress& address) const noexcept
{
    // FNV-1a over the bytes that define identity. The engine's other id hashes
    // use the same basis, so this reads the same way as they do.
    std::uint64_t folded = 1469598103934665603ull;
    const auto fold = [&folded](std::uint8_t byte) {
        folded ^= byte;
        folded *= 1099511628211ull;
    };
    for (std::uint8_t byte : address.Bytes)
        fold(byte);
    fold(static_cast<std::uint8_t>(address.Port & 0xFF));
    fold(static_cast<std::uint8_t>(address.Port >> 8));
    fold(static_cast<std::uint8_t>(address.Family));
    return static_cast<std::size_t>(folded);
}
