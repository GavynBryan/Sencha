#include "workspace/LevelLocation.h"

namespace
{
constexpr std::string_view kWorld = "world";
constexpr std::string_view kGraph = "graph:";
constexpr std::string_view kZone = "zone:";
constexpr std::string_view kTransition = "transition:";
constexpr std::string_view kEntity = "entity:";

template <typename Id>
std::optional<LevelLocation> Parsed(std::optional<Id> id)
{
    return id.has_value() ? std::optional<LevelLocation>(*id) : std::nullopt;
}
} // namespace

std::string LevelLocationKey(const LevelLocation& location)
{
    struct Write
    {
        std::string operator()(WorldScenePlace) const { return std::string(kWorld); }
        std::string operator()(GraphId id) const { return std::string(kGraph) + GraphIdToString(id); }
        std::string operator()(ZoneId id) const { return std::string(kZone) + ZoneIdToString(id); }
        std::string operator()(TransitionId id) const { return std::string(kTransition) + TransitionIdToString(id); }
        std::string operator()(PersistentEntityId id) const { return std::string(kEntity) + PersistentEntityIdToString(id); }
    };
    return std::visit(Write{}, location);
}

std::optional<LevelLocation> ParseLevelLocation(std::string_view key)
{
    if (key == kWorld)
        return WorldScenePlace{};
    if (key.starts_with(kGraph))
        return Parsed(GraphIdFromString(key.substr(kGraph.size())));
    if (key.starts_with(kZone))
        return Parsed(ZoneIdFromString(key.substr(kZone.size())));
    if (key.starts_with(kTransition))
        return Parsed(TransitionIdFromString(key.substr(kTransition.size())));
    if (key.starts_with(kEntity))
        return Parsed(PersistentEntityIdFromString(key.substr(kEntity.size())));
    return std::nullopt;
}
