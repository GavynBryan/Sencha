#include <navigation/NavigationIds.h>

#include <charconv>
#include <format>

std::string NavLinkIdToString(NavLinkId id)
{
    return std::format("{:016x}", id.Value);
}

std::optional<NavLinkId> NavLinkIdFromString(std::string_view text)
{
    if (text.size() != 16)
        return std::nullopt;
    std::uint64_t value = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    // from_chars accepts uppercase; the canonical form must round-trip.
    if (value == 0 || std::format("{:016x}", value) != text)
        return std::nullopt;
    return NavLinkId{ value };
}
