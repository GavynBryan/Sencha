#include <logic/ResponsesSerializer.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/FourCC.h>
#include <ecs/World.h>
#include <logic/ResponseSet.h>
#include <logic/Responses.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/TypedComponentSerializer.h>

#include <cstring>
#include <string_view>

namespace
{
constexpr std::string_view kBindingsKey = "bindings";

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
        archive.BeginObject(std::string_view{});
        archive.Subtree(kBindingsKey, bindings != nullptr ? *bindings : EmptyResponseBindings());
        archive.End();
        return archive.Ok();
    }

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

        ResponseDefinitions& definitions = ResponseDefinitionsOf(world);
        Responses responses{};
        responses.Set = definitions.Sets.Intern(bindings, definitions.Sequences);
        responses.Disabled = definitions.Sets.StartsDisabled(responses.Set);
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
