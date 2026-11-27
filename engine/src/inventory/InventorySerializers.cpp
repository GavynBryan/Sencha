#include <inventory/InventorySerializers.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/Archive.h>
#include <core/serialization/FourCC.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/TypedComponentSerializer.h>

#include <type_traits>
#include <utility>

namespace
{
    constexpr std::string_view kDefinition = "definition";
    constexpr std::string_view kCount = "count";

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

    class ItemEntrySerializer final : public TypedComponentSerializer<ItemEntry>
    {
    public:
        std::string_view JsonKey() const override { return "ItemEntry"; }
        std::uint32_t BinaryChunkId() const override { return MakeFourCC('I', 'T', 'E', 'N'); }

        std::span<const RuntimeField> RuntimeFields() const override
        {
            return SceneComponentSerialization::AuthoredRuntimeFieldsOf<ItemEntry>();
        }

        bool SavedFormFromRowAlone() const override { return std::is_trivially_copyable_v<ItemEntry>; }

        bool Save(IWriteArchive& archive, EntityId entity, const World& world,
                  SceneSerializationContext& context) const override
        {
            const ItemEntry* entry = world.IsRegistered<ItemEntry>() ? world.TryGet<ItemEntry>(entity) : nullptr;
            if (entry == nullptr)
                return true;
            archive.BeginObject(std::string_view{});
            const bool named = !entry->Definition.IsValid()
                || (SceneFieldCodec<ItemId>::Save(archive, kDefinition, entry->Definition, &world, context)
                    && (archive.Field(kCount, entry->Count), true));
            archive.End();
            return named && archive.Ok();
        }

        bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                           SceneSerializationContext& context) override
        {
            ItemEntry entry{};
            archive.BeginObject(std::string_view{});
            if (archive.HasField(kCount))
                archive.Field(kCount, entry.Count);
            const bool named = !archive.HasField(kDefinition)
                || (SceneFieldCodec<ItemId>::Load(archive, kDefinition, entry.Definition, &world, context)
                    && entry.Definition.IsValid());
            ClampEntryCount(world, entity, entry, context);
            archive.End();
            return named && archive.Ok() && Place(world, entity, entry);
        }
    };
}

std::unique_ptr<IComponentSerializer> MakeItemEntrySerializer()
{
    return std::make_unique<ItemEntrySerializer>();
}

std::unique_ptr<IComponentSerializer> MakeItemContainerSerializer()
{
    return std::make_unique<ComponentSerializerWithoutDefault<ItemContainer>>();
}

std::unique_ptr<IComponentSerializer> MakeItemPickupSerializer()
{
    return std::make_unique<ComponentSerializerWithoutDefault<ItemPickup>>();
}
