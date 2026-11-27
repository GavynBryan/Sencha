#include <equipment/EquipmentStateWire.h>

#include <ecs/World.h>
#include <net/ReplicationWireContext.h>

namespace
{
    constexpr EquipmentWireRecord EquipmentStateWire::* kWireRecords[kEquipmentRecordCapacity] = {
        &EquipmentStateWire::R0,  &EquipmentStateWire::R1,  &EquipmentStateWire::R2,  &EquipmentStateWire::R3,
        &EquipmentStateWire::R4,  &EquipmentStateWire::R5,  &EquipmentStateWire::R6,  &EquipmentStateWire::R7,
        &EquipmentStateWire::R8,  &EquipmentStateWire::R9,  &EquipmentStateWire::R10, &EquipmentStateWire::R11,
        &EquipmentStateWire::R12, &EquipmentStateWire::R13, &EquipmentStateWire::R14, &EquipmentStateWire::R15,
    };
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
    for (std::uint8_t i = 0; i < state.Count && i < kEquipmentRecordCapacity; ++i)
    {
        const EquipmentRecord& record = state.Records[i];
        EquipmentWireRecord& out = wire.*kWireRecords[i];
        out.Words.Values[0] = record.Key;
        out.Words.Values[1] = items != nullptr ? items->WireKey(record.Definition) : 0u;
        out.Words.Values[2] = record.Slots;
        out.Words.Values[3] = record.Primary;
        out.Source = context.WireEntity(ResolvePersistableRef(world, record.Source));
    }
}

void ReplicationCodec<EquipmentState>::FromWire(const ReplicationWireContext& context, const EquipmentStateWire& wire,
                                                EquipmentState& state)
{
    const World* world = context.Entities;
    const EquipmentLayoutRegistry* layouts = world != nullptr ? world->TryGetResource<EquipmentLayoutRegistry>() : nullptr;
    const ItemRegistry* items = world != nullptr ? world->TryGetResource<ItemRegistry>() : nullptr;
    state.Layout = layouts != nullptr ? layouts->FindByWireKey(wire.Layout) : EquipmentLayoutId{};
    state.Count = 0;
    for (const auto member : kWireRecords)
    {
        const EquipmentWireRecord& in = wire.*member;
        if (in.Words.Values[0] == 0)
            continue;
        EquipmentRecord& record = state.Records[state.Count++];
        record = EquipmentRecord{};
        record.Key = in.Words.Values[0];
        record.Definition = items != nullptr ? items->FindByWireKey(in.Words.Values[1]) : ItemId{};
        record.Slots = in.Words.Values[2];
        record.Primary = static_cast<std::uint8_t>(in.Words.Values[3]);
        record.Source = PersistableRef{ .Entity = context.LocalEntity(in.Source), .Target = {} };
    }
}
