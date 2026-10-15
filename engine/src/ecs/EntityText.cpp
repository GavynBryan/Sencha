#include <ecs/EntityText.h>

#include <charconv>
#include <cstdio>

void AppendEntityText(std::string& out, EntityId entity)
{
    char buffer[48];
    const int length = std::snprintf(buffer, sizeof(buffer), "%u:%u",
                                     entity.Index, entity.Generation);
    if (length > 0)
        out.append(buffer, static_cast<std::size_t>(length));
}

std::optional<EntityId> ParseEntityText(std::string_view text)
{
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos)
        return std::nullopt;
    const auto parse = [](std::string_view part, std::uint32_t& out) {
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), out);
        return error == std::errc{} && end == part.data() + part.size();
    };
    EntityId entity;
    if (!parse(text.substr(0, colon), entity.Index) || !parse(text.substr(colon + 1), entity.Generation))
        return std::nullopt;
    return entity;
}
