#include <equipment/EquipmentStateWire.h>

#include <ecs/World.h>
#include <net/ReplicationWireContext.h>

namespace
{
    constexpr EquipmentWireWords EquipmentStateWire::* kWireRecords[kEquipmentRecordCapacity] = {
        &EquipmentStateWire::R0, &EquipmentStateWire::R1, &EquipmentStateWire::R2, &EquipmentStateWire::R3,
        &EquipmentStateWire::R4, &EquipmentStateWire::R5, &EquipmentStateWire::R6, &EquipmentStateWire::R7,
        &EquipmentStateWire::R8, &EquipmentStateWire::R9, &EquipmentStateWire::R10, &EquipmentStateWire::R11,
        &EquipmentStateWire::R12, &EquipmentStateWire::R13, &EquipmentStateWire::R14, &EquipmentStateWire::R15,
        &EquipmentStateWire::R16, &EquipmentStateWire::R17, &EquipmentStateWire::R18, &EquipmentStateWire::R19,
        &EquipmentStateWire::R20, &EquipmentStateWire::R21, &EquipmentStateWire::R22, &EquipmentStateWire::R23,
        &EquipmentStateWire::R24, &EquipmentStateWire::R25, &EquipmentStateWire::R26, &EquipmentStateWire::R27,
        &EquipmentStateWire::R28, &EquipmentStateWire::R29, &EquipmentStateWire::R30, &EquipmentStateWire::R31,
    };

    constexpr EquipmentWireSources EquipmentStateWire::* kWireSources[] = {
        &EquipmentStateWire::S0, &EquipmentStateWire::S1, &EquipmentStateWire::S2, &EquipmentStateWire::S3,
        &EquipmentStateWire::S4, &EquipmentStateWire::S5, &EquipmentStateWire::S6, &EquipmentStateWire::S7,
        &EquipmentStateWire::S8, &EquipmentStateWire::S9, &EquipmentStateWire::S10, &EquipmentStateWire::S11,
        &EquipmentStateWire::S12, &EquipmentStateWire::S13, &EquipmentStateWire::S14, &EquipmentStateWire::S15,
    };

    template <typename Wire>
    auto& SourceOf(Wire& wire, std::size_t record)
    {
        return (wire.*kWireSources[record / kEquipmentSourcesPerWireRun]).Values[record % kEquipmentSourcesPerWireRun];
    }
}

void ReplicationCodec<EquipmentState>::ToWire(const ReplicationWireContext& context, const EquipmentState& state,
                                              EquipmentStateWire& wire)
{
    wire = EquipmentStateWire{};
    if (context.Entities == nullptr)
        return;
    const World& world = *context.Entities;
    const EquipmentLayoutRegistry* layouts = world.TryGetResource<EquipmentLayoutRegistry>();
    const ItemRegistry* items = world.TryGetResource<ItemRegistry>();
    wire.Layout = layouts != nullptr ? layouts->WireKey(state.Layout) : 0u;
    const std::span<const EquipmentRecord> records = EquipmentRecordsOf(world, state);
    for (std::size_t i = 0; i < records.size() && i < kEquipmentRecordCapacity; ++i)
    {
        const EquipmentRecord& record = records[i];
        EquipmentWireWords& out = wire.*kWireRecords[i];
        out.Values[0] = record.Key;
        out.Values[1] = items != nullptr ? items->WireKey(record.Definition) : 0u;
        out.Values[2] = record.Slots;
        out.Values[3] = record.Primary;
        SourceOf(wire, i) = context.WireEntity(ResolvePersistableRef(world, record.Source));
    }
}

void ReplicationCodec<EquipmentState>::FromWire(const ReplicationWireContext& context, const EquipmentStateWire& wire,
                                                EquipmentState& state)
{
    World* world = context.Receiving;
    if (world == nullptr)
        return;
    const EquipmentLayoutRegistry* layouts = world->TryGetResource<EquipmentLayoutRegistry>();
    const ItemRegistry* items = world->TryGetResource<ItemRegistry>();
    EquipmentRecordList worn;
    worn.Layout = layouts != nullptr ? layouts->FindByWireKey(wire.Layout) : EquipmentLayoutId{};
    worn.NextKey = state.NextKey;
    for (std::size_t i = 0; i < kEquipmentRecordCapacity; ++i)
    {
        const EquipmentWireWords& in = wire.*kWireRecords[i];
        if (in.Values[0] == 0)
            continue;
        EquipmentRecord record;
        record.Key = in.Values[0];
        record.Definition = items != nullptr ? items->FindByWireKey(in.Values[1]) : ItemId{};
        record.Slots = in.Values[2];
        record.Primary = static_cast<std::uint8_t>(in.Values[3]);
        record.Source = PersistableRef{ .Entity = context.LocalEntity(SourceOf(wire, i)), .Target = {} };
        worn.Records.push_back(record);
    }
    state = MakeEquipmentState(*world, worn);
}
