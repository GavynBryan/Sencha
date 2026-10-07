#include <core/assets/AssetId.h>

#include <core/hash/ContentHash.h>

#include <charconv>
#include <format>

std::string AssetIdToString(AssetId id)
{
    return std::format("{:016x}", id.Value);
}

std::optional<AssetId> AssetIdFromString(std::string_view text)
{
    if (text.size() != 16)
        return std::nullopt;

    uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;

    // from_chars accepts leading '-' and "0x" never reaches here, but it
    // does accept uppercase hex; the strict round-trip rule is lowercase
    // only, so a re-format must reproduce the input.
    AssetId id{ value };
    if (!id.IsValid() || AssetIdToString(id) != text)
        return std::nullopt;
    return id;
}

AssetId AssetIdOfPath(std::string_view path, uint64_t salt)
{
    AssetId id{ HashBytes64(path, salt) };
    while (!id.IsValid())
        id.Value = HashBytes64(path, ++salt);
    return id;
}

AssetId AssetIdOfPart(AssetId file, std::string_view part)
{
    AssetId id{ HashBytes64(part, file.Value) };
    for (uint64_t salt = file.Value + 1; !id.IsValid(); ++salt)
        id.Value = HashBytes64(part, salt);
    return id;
}
