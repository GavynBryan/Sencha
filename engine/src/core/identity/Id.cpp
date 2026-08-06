#include <core/identity/Id.h>

#include <charconv>
#include <format>

std::string PersistentEntityIdToString(PersistentEntityId id)
{
    return std::format("{:016x}", id.Value);
}

std::optional<PersistentEntityId> PersistentEntityIdFromString(std::string_view text)
{
    if (text.size() != 16)
        return std::nullopt;

    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;

    // from_chars accepts uppercase hex; the strict round-trip rule is
    // lowercase only, so a re-format must reproduce the input.
    PersistentEntityId id{ value };
    if (!id.IsValid() || PersistentEntityIdToString(id) != text)
        return std::nullopt;
    return id;
}
