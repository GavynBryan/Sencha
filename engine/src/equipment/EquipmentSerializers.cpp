#include <equipment/EquipmentSerializers.h>

#include <core/metadata/DataSchema.h>
#include <core/serialization/Archive.h>
#include <core/serialization/FourCC.h>
#include <equipment/EquipmentComponents.h>
#include <world/serialization/SceneAssetRef.h>
#include <world/serialization/TypedComponentSerializer.h>

#include <algorithm>
#include <bit>
#include <string>
#include <vector>

namespace
{
    constexpr std::string_view kLayout = "layout";
    constexpr std::string_view kNextKey = "next_key";
    constexpr std::string_view kRecords = "records";
    constexpr std::string_view kKey = "key";
    constexpr std::string_view kDefinition = "definition";
    constexpr std::string_view kSlots = "slots";
    constexpr std::string_view kSource = "source";

    // Records replace whole on a patch; their sources are entities a placement renames.
    DataFieldSchema MakeCompositionSchema()
    {
        DataFieldSchema record = MakeDataField(DataFieldKind::Record, {}, {});
        record.Children.push_back(MakeDataField(DataFieldKind::Entity, std::string(kSource), {}));
        DataFieldSchema records = MakeDataField(DataFieldKind::Array, std::string(kRecords), {});
        records.Children.push_back(std::move(record));
        DataFieldSchema state = MakeDataField(DataFieldKind::Record, {}, {});
        state.Children.push_back(std::move(records));
        return state;
    }

    // The primary slot first, so a load finds it where it was.
    std::vector<std::uint8_t> SlotsInOrder(const EquipmentRecord& record)
    {
        std::vector<std::uint8_t> slots;
        if (record.Primary != kNoEquipmentSlot)
            slots.push_back(record.Primary);
        for (std::uint32_t mask = record.Slots; mask != 0; mask &= mask - 1)
            if (const auto slot = static_cast<std::uint8_t>(std::countr_zero(mask)); slot != record.Primary)
                slots.push_back(slot);
        return slots;
    }

    bool WriteRecord(IWriteArchive& archive, const EquipmentRecord& record, const EquipmentLayout* layout,
                     const World& world, SceneSerializationContext& context)
    {
        archive.BeginObject(std::string_view{});
        archive.Field(kKey, record.Key);
        if (!WriteSceneDefinitionRef(archive, kDefinition, world.TryGetResource<ItemRegistry>(), record.Definition,
                                     context))
            return false;
        const std::vector<std::uint8_t> slots = SlotsInOrder(record);
        archive.BeginArray(kSlots, slots.size());
        for (const std::uint8_t slot : slots)
        {
            if (layout == nullptr || slot >= layout->Slots.size())
            {
                archive.MarkInvalidField(kSlots);
                return false;
            }
            archive.Field(std::string_view{}, std::string_view(layout->Slots[slot].Key));
        }
        archive.End();
        // The id the source carries now: a moved or changed entry may have taken a runtime one.
        PersistableRef source = MakePersistableRef(world, ResolvePersistableRef(world, record.Source));
        if (!source.Target.IsValid())
            source = record.Source;
        if (!SceneFieldCodec<PersistableRef>::Save(archive, kSource, source, context))
            return false;
        archive.End();
        return true;
    }

    bool ReadRecord(IReadArchive& archive, EquipmentRecord& record, std::uint32_t& held, const EquipmentLayout& layout,
                    World& world, SceneSerializationContext& context)
    {
        archive.BeginObject(std::string_view{});
        archive.Field(kKey, record.Key);
        if (!ReadSceneDefinitionRef(archive, kDefinition, world.TryGetResource<ItemRegistry>(), record.Definition,
                                    context))
            return false;
        std::size_t slots = 0;
        archive.BeginArray(kSlots, slots);
        for (std::size_t s = 0; s < slots; ++s)
        {
            std::string key;
            archive.Field(std::string_view{}, key);
            const std::uint8_t slot = FindEquipmentSlot(layout, key);
            if (slot == kNoEquipmentSlot || ((held | record.Slots) >> slot) & 1u)
            {
                archive.MarkInvalidField(kSlots);
                return false;
            }
            record.Slots |= 1u << slot;
            if (s == 0)
                record.Primary = slot;
        }
        archive.End();
        if (record.Slots == 0)
        {
            archive.MarkInvalidField(kSlots);
            return false;
        }
        held |= record.Slots;
        if (archive.HasField(kSource) && !SceneFieldCodec<PersistableRef>::Load(archive, kSource, record.Source, context))
            return false;
        archive.End();
        return true;
    }

    bool ReadRecords(IReadArchive& archive, EquipmentRecordList& worn, World& world, SceneSerializationContext& context)
    {
        const EquipmentLayout* layout = FindEquipmentLayout(world, worn.Layout);
        std::size_t count = 0;
        std::uint32_t held = 0;
        archive.BeginArray(kRecords, count);
        if (count > kEquipmentRecordCapacity || (count > 0 && layout == nullptr))
        {
            archive.MarkInvalidField(kRecords);
            return false;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            EquipmentRecord record;
            if (!ReadRecord(archive, record, held, *layout, world, context))
                return false;
            const bool reused = std::ranges::find(worn.Records, record.Key, &EquipmentRecord::Key) != worn.Records.end();
            if (record.Key == 0 || record.Key >= worn.NextKey || reused)
            {
                archive.MarkInvalidField(kKey);
                return false;
            }
            worn.Records.push_back(record);
        }
        archive.End();
        return true;
    }

    class EquipmentStateSerializer final : public TypedComponentSerializer<EquipmentState>
    {
    public:
        std::string_view JsonKey() const override { return "EquipmentState"; }
        std::uint32_t BinaryChunkId() const override { return MakeFourCC('E', 'Q', 'S', 'T'); }
        std::span<const RuntimeField> RuntimeFields() const override { return {}; }

        const DataFieldSchema* CompositionSchema() const override
        {
            static const DataFieldSchema schema = MakeCompositionSchema();
            return &schema;
        }

        bool Save(IWriteArchive& archive, EntityId entity, const World& world,
                  SceneSerializationContext& context) const override
        {
            const EquipmentState* state = world.IsRegistered<EquipmentState>() ? world.TryGet<EquipmentState>(entity)
                                                                               : nullptr;
            if (state == nullptr)
                return true;
            archive.BeginObject(std::string_view{});
            if (!WriteSceneDefinitionRef(archive, kLayout, world.TryGetResource<EquipmentLayoutRegistry>(),
                                         state->Layout, context))
                return false;
            archive.Field(kNextKey, state->NextKey);
            const EquipmentLayout* layout = FindEquipmentLayout(world, state->Layout);
            const std::span<const EquipmentRecord> records = EquipmentRecordsOf(world, *state);
            archive.BeginArray(kRecords, records.size());
            for (const EquipmentRecord& record : records)
                if (!WriteRecord(archive, record, layout, world, context))
                    return false;
            archive.End();
            archive.End();
            return archive.Ok();
        }

        bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                           SceneSerializationContext& context) override
        {
            EquipmentRecordList worn;
            archive.BeginObject(std::string_view{});
            if (!ReadSceneDefinitionRef(archive, kLayout, world.TryGetResource<EquipmentLayoutRegistry>(), worn.Layout,
                                        context))
                return false;
            if (archive.HasField(kNextKey))
                archive.Field(kNextKey, worn.NextKey);
            if (archive.HasField(kRecords) && !ReadRecords(archive, worn, world, context))
                return false;
            archive.End();
            return archive.Ok() && Place(world, entity, MakeEquipmentState(world, worn));
        }
    };
}

std::unique_ptr<IComponentSerializer> MakeEquipmentStateSerializer()
{
    return std::make_unique<EquipmentStateSerializer>();
}
