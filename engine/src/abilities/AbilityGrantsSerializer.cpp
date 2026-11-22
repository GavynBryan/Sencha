#include <abilities/AbilityGrantsSerializer.h>

#include <abilities/AbilityBindingNames.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityRegistry.h>
#include <core/serialization/Archive.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <world/serialization/SceneAssetRef.h>
#include <world/serialization/TypedComponentSerializer.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr std::string_view kGrants = "grants";
    constexpr std::string_view kKey = "key";
    constexpr std::string_view kAbility = "ability";
    constexpr std::string_view kBinding = "binding";
    constexpr std::string_view kParameters = "parameters";
    constexpr std::string_view kValue = "value";
    constexpr std::string_view kKind = "kind";
    constexpr std::string_view kRequested = "request";
    constexpr std::string_view kSource = "source";

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

    void ReadParameters(IReadArchive& archive, std::vector<AbilityGrantParameter>& parameters)
    {
        std::size_t count = 0;
        archive.BeginArray(kParameters, count);
        for (std::size_t i = 0; i < count && archive.Ok(); ++i)
        {
            AbilityGrantParameter parameter;
            archive.BeginObject(std::string_view{});
            archive.Field(kKey, parameter.Key);
            archive.Field(kValue, parameter.Value);
            archive.End();
            parameters.push_back(std::move(parameter));
        }
        archive.End();
    }

    void WriteParameters(IWriteArchive& archive, const std::vector<AbilityGrantParameter>& parameters)
    {
        if (parameters.empty())
            return;
        archive.BeginArray(kParameters, parameters.size());
        for (const AbilityGrantParameter& parameter : parameters)
        {
            archive.BeginObject(std::string_view{});
            archive.Field(kKey, std::string_view(parameter.Key));
            archive.Field(kValue, parameter.Value);
            archive.End();
        }
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

class AbilityGrantsSerializer final : public TypedComponentSerializer<AbilityGrants>
{
public:
    std::string_view JsonKey() const override { return "AbilityGrants"; }
    const DataFieldSchema* CompositionSchema() const override
    {
        static const DataFieldSchema schema = [] {
            DataFieldSchema grants = MakeKeyedListRecordSchema(std::string(kGrants), std::string(kKey));
            std::vector<DataFieldSchema>& record = grants.Children.front().Children.front().Children;
            record.push_back(
                std::move(MakeKeyedListRecordSchema(std::string(kParameters), std::string(kKey)).Children.front()));
            record.push_back(MakeDataField(DataFieldKind::Entity, std::string(kSource), {}));
            return grants;
        }();
        return &schema;
    }
    std::uint32_t BinaryChunkId() const override { return MakeFourCC('A', 'B', 'G', 'R'); }
    std::span<const RuntimeField> RuntimeFields() const override { return {}; }

    bool Save(IWriteArchive& archive, EntityId entity, const World& world,
              SceneSerializationContext& context) const override
    {
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

        const auto lasting = [](const AbilityGrant& grant) {
            return grant.Kind == AbilityGrantKind::Authored || grant.Kind == AbilityGrantKind::Request;
        };
        const std::span<const AbilityGrant> held = AbilityGrantsOf(world, *grants);
        const auto count = std::ranges::count_if(held, lasting);

        archive.BeginObject(std::string_view{});
        archive.BeginArray(kGrants, static_cast<std::size_t>(count));
        for (const AbilityGrant& grant : held)
        {
            const AbilityGrantEntry* entry = entries->Find(grant.Entry);
            if (!lasting(grant) || entry == nullptr)
                continue;
            archive.BeginObject(std::string_view{});
            archive.Field(kKey, std::string_view(entry->Key));
            if (!WriteSceneAssetRef(archive, kAbility, AssetType::Data, abilities->GetName(grant.Ability), context))
                return false;
            WriteBinding(archive, entry->Override);
            WriteParameters(archive, entry->Parameters);
            if (grant.Kind == AbilityGrantKind::Request)
                archive.Field(kKind, kRequested);
            if (grant.Source.Target.IsValid()
                && !SceneFieldCodec<PersistableRef>::Save(archive, kSource, grant.Source, context))
                return false;
            archive.End();
        }
        archive.End();
        archive.End();
        return archive.Ok();
    }

    bool SaveDefault(IWriteArchive& archive) const override
    {
        if (!archive.IsText())
        {
            archive.MarkInvalidField(kGrants);
            return false;
        }
        archive.BeginObject(std::string_view{});
        archive.BeginArray(kGrants, 0);
        archive.End();
        archive.End();
        return archive.Ok();
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

        AbilityGrantList grants;
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
            if (archive.HasField(kParameters))
                ReadParameters(archive, entry.Parameters);
            AbilityGrantKind kind = AbilityGrantKind::Authored;
            if (archive.HasField(kKind))
            {
                std::string text;
                archive.Field(kKind, text);
                if (text == kRequested)
                    kind = AbilityGrantKind::Request;
                else
                    archive.MarkInvalidField(kKind);
            }
            PersistableRef source;
            if (archive.HasField(kSource) && !SceneFieldCodec<PersistableRef>::Load(archive, kSource, source, context))
                return false;
            archive.End();
            const AbilityId ability = entry.Ability;
            const std::uint16_t index = entries->Intern(std::move(entry));
            if (index == kAbilityNoGrantEntry
                || !GrantAbility(grants, ability, kind, index, {}, source).IsValid())
                archive.MarkInvalidField(kGrants);
        }
        archive.End();
        archive.End();
        if (!archive.Ok())
            return false;

        return Place(world, entity, MakeAbilityGrants(world, grants));
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeAbilityGrantsSerializer()
{
    return std::make_unique<AbilityGrantsSerializer>();
}
