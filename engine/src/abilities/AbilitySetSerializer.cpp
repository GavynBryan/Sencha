#include <abilities/AbilitySetSerializer.h>

#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <core/serialization/Archive.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <world/serialization/IComponentSerializer.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Format: { "abilities": [ "<name>", ... ] }.
//
// Binary note: like the tag container, this is JSON-first. Binary read
// archives do not carry array element counts, so the list does not round-trip
// through that path -- the same limitation the handle codecs have.

namespace
{
    constexpr std::string_view kAbilitiesKey = "abilities";

class AbilitySetSerializer final : public IComponentSerializer
{
public:
    ComponentTypeId TypeId() const override
    {
        return ResolveComponentTypeId<AbilitySet>();
    }

    std::string_view JsonKey() const override { return "AbilitySet"; }
    std::uint32_t BinaryChunkId() const override
    {
        return MakeFourCC('A', 'B', 'L', 'S');
    }

    std::span<const RuntimeField> RuntimeFields() const override { return {}; }

    std::vector<std::byte> DefaultBytes() const override
    {
        AbilitySet value{};
        std::vector<std::byte> bytes(sizeof(AbilitySet));
        std::memcpy(bytes.data(), &value, sizeof(AbilitySet));
        return bytes;
    }

    void RegisterStorage(Registry& registry) const override
    {
        if (!registry.Components.IsRegistered<AbilitySet>())
            registry.Components.RegisterComponent<AbilitySet>();
    }

    bool HasComponent(EntityId entity, const Registry& registry) const override
    {
        return registry.Components.IsRegistered<AbilitySet>()
            && registry.Components.HasComponent<AbilitySet>(entity);
    }

    bool Save(IWriteArchive& archive,
              EntityId entity,
              const Registry& registry,
              SceneSerializationContext&) const override
    {
        if (!registry.Components.IsRegistered<AbilitySet>())
            return true;

        const AbilitySet* set = registry.Components.TryGet<AbilitySet>(entity);
        if (set == nullptr)
            return true;

        const AbilityRegistry* abilities =
            registry.Components.TryGetResource<AbilityRegistry>();
        if (abilities == nullptr)
            return false;

        archive.BeginObject(std::string_view{});
        archive.BeginArray(kAbilitiesKey, set->Count);
        for (std::uint8_t i = 0; i < set->Count; ++i)
            archive.Field(std::string_view{}, abilities->GetName(set->Abilities[i]));
        archive.End();
        archive.End();
        return archive.Ok();
    }

    bool Load(IReadArchive& archive,
              EntityId entity,
              Registry& registry,
              SceneSerializationContext& context) override
    {
        return LoadIntoWorld(archive, entity, registry.Components, context);
    }

    bool LoadIntoWorld(IReadArchive& archive,
                       EntityId entity,
                       World& world,
                       SceneSerializationContext&) override
    {
        const AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
        if (abilities == nullptr)
            return false;

        AbilitySet set{};

        archive.BeginObject(std::string_view{});
        std::size_t count = 0;
        archive.BeginArray(kAbilitiesKey, count);
        for (std::size_t i = 0; i < count; ++i)
        {
            std::string name;
            archive.Field(std::string_view{}, name);
            if (!archive.Ok())
                break;

            // An ability this build does not define is skipped rather than
            // refused: content may name more than one game knows.
            if (const AbilityId id = abilities->Find(name); id.IsValid())
                (void)set.Grant(id);
        }
        archive.End();
        archive.End();
        if (!archive.Ok())
            return false;

        if (world.HasComponent<AbilitySet>(entity))
            return world.InitializeComponent<AbilitySet>(entity, set);

        world.AddComponent<AbilitySet>(entity, set);
        return true;
    }

    bool Remove(EntityId entity, Registry& registry) const override
    {
        if (registry.Components.IsRegistered<AbilitySet>()
            && registry.Components.HasComponent<AbilitySet>(entity))
        {
            registry.Components.RemoveComponent<AbilitySet>(entity);
        }
        return true;
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeAbilitySetSerializer()
{
    return std::make_unique<AbilitySetSerializer>();
}
