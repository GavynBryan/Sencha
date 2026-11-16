#include <logic/CounterSerializer.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/Archive.h>
#include <core/serialization/FourCC.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/Counter.h>
#include <world/serialization/TypedComponentSerializer.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

namespace
{
class CounterSerializer final : public TypedComponentSerializer<Counter>
{
public:
    std::string_view JsonKey() const override { return "Counter"; }
    std::uint32_t BinaryChunkId() const override { return MakeFourCC('C', 'N', 'T', 'R'); }

    std::span<const RuntimeField> RuntimeFields() const override
    {
        static const std::array kFields{ RuntimeField{ .Name = "value",
                                                       .Offset = offsetof(Counter, Value),
                                                       .Size = sizeof(std::int32_t),
                                                       .Scalar = FieldScalar::Int32,
                                                       .ReadOnly = true } };
        return kFields;
    }

    bool Save(IWriteArchive& archive, EntityId entity, const World& world, SceneSerializationContext&) const override
    {
        const Counter* counter = world.IsRegistered<Counter>() ? world.TryGet<Counter>(entity) : nullptr;
        if (counter == nullptr)
            return true;
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        if (tags == nullptr)
            return false;
        archive.BeginObject(std::string_view{});
        archive.Field(std::string_view{ "name" }, tags->GetName(counter->Name));
        archive.Field(std::string_view{ "value" }, static_cast<double>(counter->Value));
        archive.End();
        return archive.Ok();
    }

    bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                       SceneSerializationContext& context) override
    {
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        if (tags == nullptr)
            return false;
        std::string name;
        double value = 0.0;
        archive.BeginObject(std::string_view{});
        archive.Field(std::string_view{ "name" }, name);
        archive.Field(std::string_view{ "value" }, value);
        archive.End();
        if (!archive.Ok())
            return false;
        Counter counter{ .Name = tags->FindTag(name), .Value = 0 };
        if (!counter.Name.IsValid())
        {
            context.Logging->GetLogger<SceneSerializationContext>().Warn(
                "counter '{}' names a gameplay tag this content does not declare; it was not loaded", name);
            return false;
        }
        if (value != std::trunc(value) || value < std::numeric_limits<std::int32_t>::min()
            || value > std::numeric_limits<std::int32_t>::max())
        {
            archive.MarkInvalidField("value");
            return false;
        }
        counter.Value = static_cast<std::int32_t>(value);
        return Place(world, entity, counter);
    }
};
}

std::unique_ptr<IComponentSerializer> MakeCounterSerializer()
{
    return std::make_unique<CounterSerializer>();
}
