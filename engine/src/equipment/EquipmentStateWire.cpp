#include <equipment/EquipmentStateWire.h>

#include <ecs/World.h>
#include <net/ReplicationWireContext.h>

void ReplicationCodec<EquipmentState>::ToWire(const ReplicationWireContext& context, const EquipmentState& state,
                                              ReplicationImage& wire)
{
    if (context.Entities == nullptr)
        return;
    const World& world = *context.Entities;
    const EquipmentLayoutRegistry* layouts = world.TryGetResource<EquipmentLayoutRegistry>();
    const ItemRegistry* items = world.TryGetResource<ItemRegistry>();
    wire.HeadAs<EquipmentStateWireHead>().Layout = layouts != nullptr ? layouts->WireKey(state.Layout) : 0u;
    const std::span<const EquipmentRecord> records = EquipmentRecordsOf(world, state);
    const std::span<EquipmentRecordWire> sent = wire.Resize<EquipmentRecordWire>(0, records.size());
    for (std::size_t i = 0; i < sent.size(); ++i)
    {
        const EquipmentRecord& record = records[i];
        std::uint32_t* words = sent[i].Words.Values;
        words[0] = record.Key;
        words[1] = items != nullptr ? items->WireKey(record.Definition) : 0u;
        words[2] = record.Slots;
        words[3] = record.Primary;
        sent[i].Source = context.WireEntity(ResolvePersistableRef(world, record.Source));
    }
}

void ReplicationCodec<EquipmentState>::FromWire(const ReplicationWireContext& context, const ReplicationImage& wire,
                                                EquipmentState& state)
{
    World* world = context.Receiving;
    if (world == nullptr)
        return;
    const EquipmentLayoutRegistry* layouts = world->TryGetResource<EquipmentLayoutRegistry>();
    const ItemRegistry* items = world->TryGetResource<ItemRegistry>();
    EquipmentRecordList worn;
    worn.Layout = layouts != nullptr ? layouts->FindByWireKey(wire.HeadAs<EquipmentStateWireHead>().Layout)
                                     : EquipmentLayoutId{};
    worn.NextKey = state.NextKey;
    for (const EquipmentRecordWire& sent : wire.List<EquipmentRecordWire>(0))
    {
        const std::uint32_t* words = sent.Words.Values;
        if (words[0] == 0)
            continue;
        EquipmentRecord record;
        record.Key = words[0];
        record.Definition = items != nullptr ? items->FindByWireKey(words[1]) : ItemId{};
        record.Slots = words[2];
        record.Primary = static_cast<std::uint8_t>(words[3]);
        record.Source = PersistableRef{ .Entity = context.LocalEntity(sent.Source), .Target = {} };
        worn.Records.push_back(record);
    }
    state = MakeEquipmentState(*world, worn);
}
