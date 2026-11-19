#include <inventory/InventorySerializers.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/Archive.h>
#include <core/serialization/FourCC.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <world/serialization/SceneAssetRef.h>
#include <world/serialization/TypedComponentSerializer.h>

#include <array>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
    constexpr std::string_view kDefinition = "definition";
    constexpr std::string_view kCount = "count";
    constexpr std::string_view kCapacity = "capacity";
    constexpr std::string_view kAccepts = "accepts";

    std::uint32_t ReadCount(IReadArchive& archive, std::string_view key, std::uint32_t fallback)
    {
        std::uint32_t value = fallback;
        if (archive.HasField(key))
            archive.Field(key, value);
        return value;
    }

    RuntimeField CountField(std::size_t offset)
    {
        return RuntimeField{ .Name = "count", .Offset = offset, .Size = sizeof(std::uint32_t),
                             .Scalar = FieldScalar::UInt32, .ReadOnly = true };
    }

    void ClampEntryCount(const World& world, EntityId entity, ItemEntry& entry, SceneSerializationContext& context)
    {
        const ItemDefinition* item = FindItemDefinition(world, entry.Definition);
        if (item == nullptr || ItemStackHolds(*item, entry.Count))
            return;
        const std::uint32_t held = std::exchange(entry.Count, ItemStackClamp(*item, entry.Count));
        context.Logging->GetLogger<ItemEntry>().Warn(
            "inventory: entry {}:{} of '{}' named a count of {}, which its stack cannot hold; it loads holding {}",
            entity.Index, entity.Generation, world.TryGetResource<ItemRegistry>()->GetName(entry.Definition), held,
            entry.Count);
    }

    template<typename T>
    class CountedSerializer final : public TypedComponentSerializer<T>
    {
    public:
        CountedSerializer(std::string_view key, std::uint32_t chunk)
            : Key(key)
            , Chunk(chunk)
        {
        }

        std::string_view JsonKey() const override { return Key; }
        std::uint32_t BinaryChunkId() const override { return Chunk; }

        std::span<const RuntimeField> RuntimeFields() const override
        {
            static const std::array kFields{ CountField(offsetof(T, Count)) };
            return kFields;
        }

        bool Save(IWriteArchive& archive, EntityId entity, const World& world,
                  SceneSerializationContext& context) const override
        {
            const T* value = world.IsRegistered<T>() ? world.TryGet<T>(entity) : nullptr;
            if (value == nullptr)
                return true;
            archive.BeginObject(std::string_view{});
            const bool named = !value->Definition.IsValid()
                || (WriteSceneDefinitionRef(archive, kDefinition, world.TryGetResource<ItemRegistry>(),
                                            value->Definition, context)
                    && (archive.Field(kCount, value->Count), true));
            archive.End();
            return named && archive.Ok();
        }

        bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                           SceneSerializationContext& context) override
        {
            T value{};
            archive.BeginObject(std::string_view{});
            value.Count = ReadCount(archive, kCount, value.Count);
            const bool named = !archive.HasField(kDefinition)
                || ReadSceneDefinitionRef(archive, kDefinition, world.TryGetResource<ItemRegistry>(), value.Definition,
                                          context);
            if constexpr (std::is_same_v<T, ItemEntry>)
                ClampEntryCount(world, entity, value, context);
            archive.End();
            return named && archive.Ok() && this->Place(world, entity, value);
        }

    private:
        std::string_view Key;
        std::uint32_t Chunk;
    };

    class ItemContainerSerializer final : public TypedComponentSerializer<ItemContainer>
    {
    public:
        std::string_view JsonKey() const override { return "ItemContainer"; }
        std::uint32_t BinaryChunkId() const override { return MakeFourCC('I', 'T', 'C', 'N'); }

        std::span<const RuntimeField> RuntimeFields() const override { return {}; }

        bool Save(IWriteArchive& archive, EntityId entity, const World& world, SceneSerializationContext&) const override
        {
            const ItemContainer* container = world.IsRegistered<ItemContainer>() ? world.TryGet<ItemContainer>(entity) : nullptr;
            if (container == nullptr)
                return true;
            archive.BeginObject(std::string_view{});
            archive.Field(kCapacity, static_cast<std::uint32_t>(container->Capacity));
            const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
            if (container->Accepts.IsValid() && tags != nullptr)
                archive.Field(kAccepts, tags->GetName(container->Accepts));
            archive.End();
            return archive.Ok();
        }

        bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world, SceneSerializationContext&) override
        {
            ItemContainer container{};
            archive.BeginObject(std::string_view{});
            const std::uint32_t capacity = ReadCount(archive, kCapacity, 0);
            if (capacity > 0xFFFF)
                archive.MarkInvalidField(kCapacity);
            container.Capacity = static_cast<std::uint16_t>(capacity);
            if (archive.HasField(kAccepts))
            {
                std::string name;
                archive.Field(kAccepts, name);
                const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
                container.Accepts = tags != nullptr ? tags->FindTag(name) : GameplayTagId{};
                if (!container.Accepts.IsValid())
                    archive.MarkInvalidField(kAccepts);
            }
            archive.End();
            return archive.Ok() && Place(world, entity, container);
        }
    };
}

std::unique_ptr<IComponentSerializer> MakeItemEntrySerializer()
{
    return std::make_unique<CountedSerializer<ItemEntry>>("ItemEntry", MakeFourCC('I', 'T', 'E', 'N'));
}

std::unique_ptr<IComponentSerializer> MakeItemContainerSerializer()
{
    return std::make_unique<ItemContainerSerializer>();
}

std::unique_ptr<IComponentSerializer> MakeItemPickupSerializer()
{
    return std::make_unique<CountedSerializer<ItemPickup>>("ItemPickup", MakeFourCC('I', 'T', 'P', 'K'));
}
