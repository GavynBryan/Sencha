#include <zone/WorldPartitionIds.h>

#include <charconv>
#include <cstdint>
#include <format>

namespace
{

std::string HexEncode(uint64_t value)
{
    return std::format("{:016x}", value);
}

// Strict inverse of HexEncode: exactly 16 hex digits, nonzero. from_chars
// accepts uppercase hex, so a re-format must reproduce the input exactly.
std::optional<uint64_t> HexDecode(std::string_view text)
{
    if (text.size() != 16)
        return std::nullopt;

    uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;

    if (value == 0 || HexEncode(value) != text)
        return std::nullopt;
    return value;
}

} // namespace

std::string ZoneIdToString(ZoneId id)
{
    return HexEncode(id.Value);
}

std::optional<ZoneId> ZoneIdFromString(std::string_view text)
{
    const auto value = HexDecode(text);
    if (!value)
        return std::nullopt;
    return ZoneId{ *value };
}

std::string RegionIdToString(RegionId id)
{
    return HexEncode(id.Value);
}

std::optional<RegionId> RegionIdFromString(std::string_view text)
{
    const auto value = HexDecode(text);
    if (!value)
        return std::nullopt;
    return RegionId{ *value };
}

std::string TransitionIdToString(TransitionId id)
{
    return HexEncode(id.Value);
}

std::optional<TransitionId> TransitionIdFromString(std::string_view text)
{
    const auto value = HexDecode(text);
    if (!value)
        return std::nullopt;
    return TransitionId{ *value };
}
