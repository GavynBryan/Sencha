#include <abilities/AbilityGrantsSerializer.h>

#include <abilities/AbilityBindingNames.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityRegistry.h>
#include <core/serialization/Archive.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <world/serialization/SceneAssetRef.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr std::string_view kGrants = "grants";
    constexpr std::string_view kKey = "key";
    constexpr std::string_view kAbility = "ability";
    constexpr std::string_view kBinding = "binding";

    template<typename E, std::size_t N>
    void ReadChoice(IReadArchive& archive, std::string_view field, const std::array<AbilityNamed<E>, N>& names,
                    std::optional<E>& out)
    {
        if (!archive.HasField(field))
            return;
        std::string text;
        archive.Field(field, text);
        out = FindAbilityNamed(names, text);
        if (!out)
            archive.MarkInvalidField(field);
    }

    template<typename E, std::size_t N>
    void WriteChoice(IWriteArchive& archive, std::string_view field, const std::array<AbilityNamed<E>, N>& names,
                     const std::optional<E>& value)
    {
        if (value)
            archive.Field(field, AbilityNameOf(names, *value));
    }

    void ReadNames(IReadArchive& archive, std::string_view field, std::vector<std::string>& out)
    {
        if (!archive.HasField(field))
            return;
        std::size_t count = 0;
        archive.BeginArray(field, count);
        out.resize(count);
        for (std::string& name : out)
            archive.Field(std::string_view{}, name);
        archive.End();
    }

    void WriteNames(IWriteArchive& archive, std::string_view field, const std::vector<std::string>& names)
    {
        if (names.empty())
            return;
        archive.BeginArray(field, names.size());
        for (const std::string& name : names)
            archive.Field(std::string_view{}, std::string_view(name));
        archive.End();
    }

    void ReadBinding(IReadArchive& archive, AbilityBindingDecl& binding)
    {
        archive.BeginObject(kBinding);
        if (archive.HasField(std::string_view{ "action" }))
        {
            std::string action;
            archive.Field(std::string_view{ "action" }, action);
            binding.Action = action;
        }
        ReadChoice(archive, std::string_view{ "policy" }, kAbilityInputPolicyNames, binding.Policy);
        if (archive.HasField(std::string_view{ "priority" }))
        {
            double priority = 0.0;
            archive.Field(std::string_view{ "priority" }, priority);
            binding.Priority = static_cast<std::int32_t>(priority);
        }
        ReadChoice(archive, std::string_view{ "consume" }, kAbilityConsumeNames, binding.Consume);
        ReadChoice(archive, std::string_view{ "when_blocked" }, kAbilityWhenBlockedNames, binding.WhenBlocked);
        if (archive.HasField(std::string_view{ "context" }))
        {
            AbilityTagQueryDecl context;
            archive.BeginObject(std::string_view{ "context" });
            ReadNames(archive, std::string_view{ "all" }, context.All);
            ReadNames(archive, std::string_view{ "any" }, context.Any);
            ReadNames(archive, std::string_view{ "none" }, context.None);
            archive.End();
            binding.Context = std::move(context);
        }
        ReadChoice(archive, std::string_view{ "on_interrupt" }, kAbilityInterruptNames, binding.OnInterrupt);
        archive.End();
    }

    void WriteBinding(IWriteArchive& archive, const AbilityBindingDecl& binding)
    {
        if (binding == AbilityBindingDecl{})
            return;
        archive.BeginObject(kBinding);
        if (binding.Action)
            archive.Field(std::string_view{ "action" }, std::string_view(*binding.Action));
        WriteChoice(archive, std::string_view{ "policy" }, kAbilityInputPolicyNames, binding.Policy);
        if (binding.Priority)
            archive.Field(std::string_view{ "priority" }, static_cast<double>(*binding.Priority));
        WriteChoice(archive, std::string_view{ "consume" }, kAbilityConsumeNames, binding.Consume);
        WriteChoice(archive, std::string_view{ "when_blocked" }, kAbilityWhenBlockedNames, binding.WhenBlocked);
        if (binding.Context)
        {
            archive.BeginObject(std::string_view{ "context" });
            WriteNames(archive, std::string_view{ "all" }, binding.Context->All);
            WriteNames(archive, std::string_view{ "any" }, binding.Context->Any);
            WriteNames(archive, std::string_view{ "none" }, binding.Context->None);
            archive.End();
        }
        WriteChoice(archive, std::string_view{ "on_interrupt" }, kAbilityInterruptNames, binding.OnInterrupt);
        archive.End();
    }

class AbilityGrantsSerializer final : public IComponentSerializer
{
public:
    ComponentTypeId TypeId() const override { return ResolveComponentTypeId<AbilityGrants>(); }
    std::string_view JsonKey() const override { return "AbilityGrants"; }
    std::uint32_t BinaryChunkId() const override { return MakeFourCC('A', 'B', 'G', 'R'); }
    std::span<const RuntimeField> RuntimeFields() const override { return {}; }

    std::vector<std::byte> DefaultBytes() const override
    {
        AbilityGrants value{};
        std::vector<std::byte> bytes(sizeof(AbilityGrants));
        std::memcpy(bytes.data(), &value, sizeof(AbilityGrants));
        return bytes;
    }

    void RegisterStorage(Registry& registry) const override
    {
        if (!registry.Components.IsRegistered<AbilityGrants>())
            registry.Components.RegisterComponent<AbilityGrants>();
    }

    bool HasComponent(EntityId entity, const Registry& registry) const override
    {
        return registry.Components.IsRegistered<AbilityGrants>()
            && registry.Components.HasComponent<AbilityGrants>(entity);
    }

    bool Save(IWriteArchive& archive, EntityId entity, const Registry& registry,
              SceneSerializationContext& context) const override
    {
        const World& world = registry.Components;
        const AbilityGrants* grants = world.IsRegistered<AbilityGrants>() ? world.TryGet<AbilityGrants>(entity) : nullptr;
        if (grants == nullptr)
            return true;
        if (!archive.IsText())
        {
            archive.MarkInvalidField(kGrants);
            return false;
        }
        const AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
        const AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
        if (abilities == nullptr || entries == nullptr)
            return false;

        std::size_t authored = 0;
        for (std::uint8_t i = 0; i < grants->Count; ++i)
            authored += grants->Entries[i].Kind == AbilityGrantKind::Authored ? 1 : 0;

        archive.BeginObject(std::string_view{});
        archive.BeginArray(kGrants, authored);
        for (std::uint8_t i = 0; i < grants->Count; ++i)
        {
            const AbilityGrant& grant = grants->Entries[i];
            const AbilityGrantEntry* entry = entries->Find(grant.Entry);
            if (grant.Kind != AbilityGrantKind::Authored || entry == nullptr)
                continue;
            archive.BeginObject(std::string_view{});
            archive.Field(kKey, std::string_view(entry->Key));
            if (!WriteSceneAssetRef(archive, kAbility, AssetType::Data, abilities->GetName(grant.Ability), context))
                return false;
            WriteBinding(archive, entry->Override);
            archive.End();
        }
        archive.End();
        archive.End();
        return archive.Ok();
    }

    bool Load(IReadArchive& archive, EntityId entity, Registry& registry, SceneSerializationContext& context) override
    {
        return LoadIntoWorld(archive, entity, registry.Components, context);
    }

    bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                       SceneSerializationContext& context) override
    {
        if (!archive.IsText())
        {
            archive.MarkInvalidField(kGrants);
            return false;
        }
        AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
        AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
        if (abilities == nullptr || entries == nullptr)
            return false;

        AbilityGrants grants{};
        archive.BeginObject(std::string_view{});
        std::size_t count = 0;
        archive.BeginArray(kGrants, count);
        for (std::size_t i = 0; i < count && archive.Ok(); ++i)
        {
            archive.BeginObject(std::string_view{});
            AbilityGrantEntry entry;
            archive.Field(kKey, entry.Key);
            std::string path;
            if (!ReadSceneAssetRef(archive, kAbility, AssetType::Data, path, context))
                return false;
            // Reserved, not looked up: a grant read before its ability is declared,
            // or of one that failed to compile, keeps its name for the next save.
            entry.Ability = abilities->Reserve(path);
            if (archive.HasField(kBinding))
                ReadBinding(archive, entry.Override);
            archive.End();
            const AbilityId ability = entry.Ability;
            const std::uint16_t index = entries->Intern(std::move(entry));
            if (index == kAbilityNoGrantEntry
                || !GrantAbility(grants, ability, AbilityGrantKind::Authored, index).IsValid())
                archive.MarkInvalidField(kGrants);
        }
        archive.End();
        archive.End();
        if (!archive.Ok())
            return false;

        if (world.HasComponent<AbilityGrants>(entity))
            return world.InitializeComponent<AbilityGrants>(entity, grants);
        world.AddComponent<AbilityGrants>(entity, grants);
        return true;
    }

    bool Remove(EntityId entity, Registry& registry) const override
    {
        if (registry.Components.IsRegistered<AbilityGrants>() && registry.Components.HasComponent<AbilityGrants>(entity))
            registry.Components.RemoveComponent<AbilityGrants>(entity);
        return true;
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeAbilityGrantsSerializer()
{
    return std::make_unique<AbilityGrantsSerializer>();
}
