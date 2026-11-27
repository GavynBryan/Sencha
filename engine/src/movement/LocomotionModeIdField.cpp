#include <movement/LocomotionModeId.h>

#include <ecs/World.h>
#include <movement/LocomotionMode.h>
#include <world/serialization/NamedIdVocabulary.h>

namespace
{
const LocomotionModeRegistry* ModesOf(const World& world)
{
    return world.TryGetResource<LocomotionModeRegistry>();
}

std::uint32_t FindMode(const World& world, std::string_view name)
{
    const LocomotionModeRegistry* modes = ModesOf(world);
    const LocomotionModeEntry* entry = modes != nullptr && !name.empty() ? modes->Find(name) : nullptr;
    return entry != nullptr ? entry->Id.Value : 0u;
}
}

const NamedIdVocabulary& NamedIdField<LocomotionModeId>::Vocabulary()
{
    static const NamedIdVocabulary vocabulary{
        .Label = "locomotion mode",
        .Kind = Kind,
        .DataSubtype = {},
        .NameOf = [](const World& world, std::uint32_t id) -> std::string_view {
            const LocomotionModeRegistry* modes = ModesOf(world);
            const LocomotionModeEntry* entry = modes != nullptr ? modes->Find(LocomotionModeId{ id }) : nullptr;
            return entry != nullptr ? std::string_view(entry->Name) : std::string_view{};
        },
        .Find = FindMode,
        .Resolve = [](World& world, std::string_view name) -> std::uint32_t {
            if (const std::uint32_t found = FindMode(world, name); found != 0)
                return found;
            const LocomotionModeRegistry* modes = ModesOf(world);
            return modes != nullptr ? modes->FreeMode().Value : 0u;
        },
        .Names = [](const World& world, std::vector<std::string_view>& out) {
            if (const LocomotionModeRegistry* modes = ModesOf(world))
                for (const LocomotionModeEntry& entry : modes->Entries())
                    out.push_back(entry.Name);
        },
        .WireKey = nullptr,
        .FromWireKey = nullptr,
    };
    return vocabulary;
}
