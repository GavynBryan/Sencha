#include <equipment/EquipmentComponents.h>

#include <ecs/World.h>

#include <cassert>

namespace
{
    EquipmentRecordLists& ListsOf(World& world)
    {
        return world.HasResource<EquipmentRecordLists>() ? world.GetResource<EquipmentRecordLists>()
                                                         : world.AddResource<EquipmentRecordLists>();
    }

    std::vector<EquipmentRecord>& Edit(World& world, EquipmentState& state)
    {
        return ListsOf(world).Edit(state.Records);
    }
}

void ComponentTraits<EquipmentState>::OnAdd(EquipmentState& state, World& world, EntityId)
{
    if (EquipmentRecordLists* lists = world.TryGetResource<EquipmentRecordLists>())
        lists->Retain(state.Records);
}

void ComponentTraits<EquipmentState>::OnRemove(const EquipmentState& state, World& world, EntityId)
{
    if (EquipmentRecordLists* lists = world.TryGetResource<EquipmentRecordLists>())
        lists->Release(state.Records);
}

std::span<const EquipmentRecord> EquipmentRecordsOf(const World& world, const EquipmentState& state)
{
    const EquipmentRecordLists* lists = world.TryGetResource<EquipmentRecordLists>();
    return lists != nullptr ? lists->Read(state.Records) : std::span<const EquipmentRecord>{};
}

std::span<const EquipmentRecord> EquipmentRecordsOf(const World& world, EntityId wearer)
{
    const EquipmentState* state =
        world.IsAlive(wearer) && world.IsRegistered<EquipmentState>() ? world.TryGet<EquipmentState>(wearer) : nullptr;
    return state != nullptr ? EquipmentRecordsOf(world, *state) : std::span<const EquipmentRecord>{};
}

std::span<EquipmentRecord> EditEquipmentRecords(World& world, EntityId wearer)
{
    EquipmentState* state = world.IsAlive(wearer) ? world.TryGet<EquipmentState>(wearer) : nullptr;
    if (state == nullptr || state->Count == 0)
        return {};
    return Edit(world, *state);
}

void RemoveEquipmentRecord(World& world, EntityId wearer, std::size_t index)
{
    EquipmentState& state = *world.TryGet<EquipmentState>(wearer);
    std::vector<EquipmentRecord>& records = Edit(world, state);
    records[index] = records.back();
    records.pop_back();
    state.Count = static_cast<std::uint8_t>(records.size());
}

std::size_t FindEquipmentRecordHolding(std::span<const EquipmentRecord> records, std::uint8_t slot)
{
    for (std::size_t i = 0; i < records.size(); ++i)
        if ((records[i].Slots >> slot) & 1u)
            return i;
    return kNoEquipmentRecord;
}

std::uint32_t WearRecord(std::vector<EquipmentRecord>& records, std::uint32_t& nextKey, EquipmentRecord record)
{
    for (std::size_t i = records.size(); i-- > 0;)
        if ((records[i].Slots & record.Slots) != 0)
        {
            records[i] = records.back();
            records.pop_back();
        }
    record.Key = nextKey++;
    records.push_back(record);
    return record.Key;
}

std::uint32_t WearEquipmentRecord(World& world, EntityId wearer, const EquipmentRecord& record)
{
    EquipmentState& state = *world.TryGet<EquipmentState>(wearer);
    std::vector<EquipmentRecord>& records = Edit(world, state);
    const std::uint32_t key = WearRecord(records, state.NextKey, record);
    assert(records.size() <= kEquipmentRecordCapacity && "a record holds a slot no other record holds");
    state.Count = static_cast<std::uint8_t>(records.size());
    return key;
}

EquipmentRecordList CopyEquipment(const World& world, EntityId wearer)
{
    EquipmentRecordList copy;
    if (const EquipmentState* state =
            world.IsRegistered<EquipmentState>() ? world.TryGet<EquipmentState>(wearer) : nullptr)
    {
        const std::span<const EquipmentRecord> records = EquipmentRecordsOf(world, *state);
        copy.Layout = state->Layout;
        copy.NextKey = state->NextKey;
        copy.Records.assign(records.begin(), records.end());
    }
    return copy;
}

EquipmentState MakeEquipmentState(World& world, const EquipmentRecordList& worn)
{
    assert(worn.Records.size() <= kEquipmentRecordCapacity);
    EquipmentState state;
    state.Layout = worn.Layout;
    state.NextKey = worn.NextKey;
    state.Count = static_cast<std::uint8_t>(worn.Records.size());
    if (!worn.Records.empty())
        state.Records = ListsOf(world).Make(worn.Records);
    return state;
}

void SetEquipment(World& world, EntityId wearer, const EquipmentRecordList& worn)
{
    const EquipmentState state = MakeEquipmentState(world, worn);
    if (world.HasComponent<EquipmentState>(wearer))
        (void)world.ReplaceComponent<EquipmentState>(wearer, state);
    else
        world.AddComponent<EquipmentState>(wearer, state);
}
