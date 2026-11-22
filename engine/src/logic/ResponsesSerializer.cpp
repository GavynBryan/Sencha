#include <logic/ResponsesSerializer.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/FourCC.h>
#include <ecs/World.h>
#include <logic/ResponseSet.h>
#include <logic/Responses.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/TypedComponentSerializer.h>

#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view kBindingsKey = "bindings";
constexpr std::string_view kEnabledKey = "enabled";
constexpr std::string_view kConsumedKey = "consumed";

bool WriteBindings(IWriteArchive& archive, const JsonValue& bindings)
{
    archive.BeginObject(std::string_view{});
    archive.Subtree(kBindingsKey, bindings);
    archive.End();
    return archive.Ok();
}

JsonValue* BindingNamed(JsonValue& bindings, std::string_view key)
{
    if (!bindings.IsObject())
        return nullptr;
    for (auto& [name, binding] : bindings.AsObject())
        if (name == key && binding.IsObject())
            return &binding;
    return nullptr;
}

void SetMember(JsonValue& binding, std::string_view key, JsonValue value)
{
    for (auto& [name, member] : binding.AsObject())
        if (name == key)
        {
            member = std::move(value);
            return;
        }
    binding.AsObject().emplace_back(std::string(key), std::move(value));
}

JsonValue WithState(const JsonValue& authored, const ResponseSetStore& sets, const Responses& state)
{
    JsonValue bindings = authored;
    const std::span<const ResponseBindingDesc> described = sets.Bindings(state.Set);
    const std::uint32_t startsDisabled = sets.StartsDisabled(state.Set);
    for (std::size_t i = 0; i < described.size(); ++i)
    {
        const std::uint32_t bit = 1u << i;
        JsonValue* binding = BindingNamed(bindings, described[i].Key);
        if (binding == nullptr)
            continue;
        if (((state.Disabled ^ startsDisabled) & bit) != 0)
            SetMember(*binding, kEnabledKey, JsonValue((state.Disabled & bit) == 0));
        if ((state.Consumed & bit) != 0)
            SetMember(*binding, kConsumedKey, JsonValue(true));
    }
    return bindings;
}

std::vector<std::string> TakeConsumed(JsonValue& bindings)
{
    std::vector<std::string> consumed;
    if (!bindings.IsObject())
        return consumed;
    for (auto& [name, binding] : bindings.AsObject())
    {
        if (!binding.IsObject())
            continue;
        auto& members = binding.AsObject();
        const auto found = std::ranges::find(members, kConsumedKey, [](const auto& member) {
            return std::string_view(member.first);
        });
        if (found == members.end())
            continue;
        if (found->second.IsBool() && found->second.AsBool())
            consumed.push_back(name);
        members.erase(found);
    }
    return consumed;
}

class ResponsesSerializer final : public TypedComponentSerializer<Responses>
{
public:
    std::string_view JsonKey() const override { return "responses"; }
    std::uint32_t BinaryChunkId() const override { return MakeFourCC('R', 'S', 'P', 'N'); }

    std::span<const RuntimeField> RuntimeFields() const override { return {}; }
    std::optional<EditorVisual> GetEditorVisual() const override { return ComponentEditorVisual<Responses>::Value; }

    bool Save(IWriteArchive& archive, EntityId entity, const World& world,
              SceneSerializationContext& context) const override
    {
        const Responses* responses = world.IsRegistered<Responses>() ? world.TryGet<Responses>(entity) : nullptr;
        if (responses == nullptr)
            return true;
        const JsonValue* bindings = ResponseBindingsSource(world, entity);
        // A key this World never interned came from bytes copied out of another
        // World; saving it as empty would lose what was authored, so it fails.
        if (bindings == nullptr && responses->Set.IsValid())
        {
            context.Logging->GetLogger<SceneSerializationContext>().Error(
                "Responses: entity's bindings are not in this World's store; not saved");
            return false;
        }
        const ResponseDefinitions* definitions = world.TryGetResource<ResponseDefinitions>();
        if (context.Purpose == SceneRecordPurpose::Save && bindings != nullptr && definitions != nullptr)
            return WriteBindings(archive, WithState(*bindings, definitions->Sets, *responses));
        return WriteBindings(archive, bindings != nullptr ? *bindings : EmptyResponseBindings());
    }

    bool SaveDefault(IWriteArchive& archive) const override { return WriteBindings(archive, EmptyResponseBindings()); }

    bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                       SceneSerializationContext& context) override
    {
        JsonValue bindings = EmptyResponseBindings();
        archive.BeginObject(std::string_view{});
        if (archive.HasField(kBindingsKey))
            archive.Subtree(kBindingsKey, bindings);
        archive.End();
        if (!archive.Ok())
            return false;

        const std::vector<std::string> consumed = TakeConsumed(bindings);
        ResponseDefinitions& definitions = ResponseDefinitionsOf(world);
        Responses responses{};
        responses.Set = definitions.Sets.Intern(bindings, definitions.Sequences);
        responses.Disabled = definitions.Sets.StartsDisabled(responses.Set);
        const std::span<const ResponseBindingDesc> described = definitions.Sets.Bindings(responses.Set);
        for (const std::string& key : consumed)
            if (const int index = FindResponseBinding(described, ResponseKeyHash(key)); index >= 0)
                responses.Consumed |= 1u << static_cast<std::uint32_t>(index);
        // What did not read is kept as authored and reported; the rest still loads.
        for (const std::string& error : definitions.Sets.Diagnostics(responses.Set))
            context.Logging->GetLogger<SceneSerializationContext>().Warn("Responses: {}", error);
        return Place(world, entity, responses);
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeResponsesSerializer()
{
    return std::make_unique<ResponsesSerializer>();
}
