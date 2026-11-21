#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <unordered_set>

void ZoneStateStore::RecordAuthoredSet(ZoneId zone, std::span<const AuthoredEntityState> authored)
{
    ZoneRecord& record = Zones_[zone];
    record.Authored.clear();
    record.Authored.reserve(authored.size());
    for (const AuthoredEntityState& entity : authored)
        if (entity.Id.IsValid())
            record.Authored.insert_or_assign(entity.Id.Value, entity.Forgotten);
}

void ZoneStateStore::RecordCapture(ZoneId zone, std::span<const PersistentEntityId> live, ZoneCapture capture)
{
    const auto it = Zones_.find(zone);
    if (it == Zones_.end())
        return;
    ZoneRecord& record = it->second;
    // Without a baseline there is nothing to diff against, and recomputing from
    // an empty one would erase the deviation already recorded.
    if (record.Authored.empty())
        return;

    std::unordered_set<std::uint64_t> liveSet;
    liveSet.reserve(live.size());
    for (const PersistentEntityId id : live)
        liveSet.insert(id.Value);

    const bool detaching = capture == ZoneCapture::Detach;
    record.Destroyed.clear();
    for (const auto& [authored, forgotten] : record.Authored)
        if (!liveSet.contains(authored) && !(detaching && forgotten == ZoneStateForgetting::AtDetach))
            record.Destroyed.emplace(authored, forgotten);
    std::erase_if(record.Components, [&](const auto& entry) {
        return !record.Authored.contains(entry.first.Entity) && !record.Destroyed.contains(entry.first.Entity);
    });
    if (detaching)
    {
        std::erase_if(record.Components,
                      [](const auto& entry) { return entry.second.Forgotten == ZoneStateForgetting::AtDetach; });
        record.Authored.clear();
    }
}

bool ZoneStateStore::IsRecordedDestroyed(ZoneId zone, PersistentEntityId id) const
{
    const auto it = Zones_.find(zone);
    return it != Zones_.end() && it->second.Destroyed.contains(id.Value);
}

std::size_t ZoneStateStore::RecordedDestroyedCount(ZoneId zone) const
{
    const auto it = Zones_.find(zone);
    return it != Zones_.end() ? it->second.Destroyed.size() : 0;
}

ZoneStateForgetting ZoneStateStore::ForgettingOf(ZoneId zone, PersistentEntityId id) const
{
    const auto it = Zones_.find(zone);
    if (it == Zones_.end())
        return ZoneStateForgetting::Never;
    if (const auto authored = it->second.Authored.find(id.Value); authored != it->second.Authored.end())
        return authored->second;
    if (const auto destroyed = it->second.Destroyed.find(id.Value); destroyed != it->second.Destroyed.end())
        return destroyed->second;
    return ZoneStateForgetting::Never;
}

void ZoneStateStore::RecordComponentState(ZoneId zone, PersistentEntityId id, ComponentTypeId type,
                                          std::uint16_t version, std::span<const std::byte> state)
{
    const ZoneStateForgetting forgotten = ForgettingOf(zone, id);
    ComponentRecord& record = Zones_[zone].Components[ComponentStateKey{ id.Value, type.Value }];
    record.Forgotten = forgotten;
    record.Version = version;
    record.Bytes.assign(state.begin(), state.end());
}

void ZoneStateStore::ForgetComponentState(ZoneId zone, PersistentEntityId id, ComponentTypeId type)
{
    if (const auto it = Zones_.find(zone); it != Zones_.end())
        it->second.Components.erase(ComponentStateKey{ id.Value, type.Value });
}

RememberedComponentState ZoneStateStore::ComponentState(ZoneId zone, PersistentEntityId id,
                                                        ComponentTypeId type) const
{
    const auto it = Zones_.find(zone);
    if (it == Zones_.end())
        return {};
    const auto found = it->second.Components.find(ComponentStateKey{ id.Value, type.Value });
    if (found == it->second.Components.end())
        return {};
    return RememberedComponentState{ found->second.Version, found->second.Bytes };
}

std::vector<DurableZoneState> ZoneStateStore::ExportDurable() const
{
    std::vector<DurableZoneState> durable;
    for (const auto& [zone, record] : Zones_)
    {
        DurableZoneState state;
        state.Zone = zone;
        for (const auto& [id, forgotten] : record.Destroyed)
            if (forgotten == ZoneStateForgetting::Never)
                state.Destroyed.push_back(PersistentEntityId{ id });
        for (const auto& [key, component] : record.Components)
            if (component.Forgotten == ZoneStateForgetting::Never)
                state.Components.push_back(DurableComponentState{ .Entity = PersistentEntityId{ key.Entity },
                                                                  .Type = ComponentTypeId{ key.Type },
                                                                  .Version = component.Version,
                                                                  .Bytes = component.Bytes });
        if (state.Destroyed.empty() && state.Components.empty())
            continue;
        std::ranges::sort(state.Destroyed, {}, &PersistentEntityId::Value);
        std::ranges::sort(state.Components, [](const DurableComponentState& a, const DurableComponentState& b) {
            return a.Entity.Value != b.Entity.Value ? a.Entity.Value < b.Entity.Value : a.Type.Value < b.Type.Value;
        });
        durable.push_back(std::move(state));
    }
    std::ranges::sort(durable, {}, [](const DurableZoneState& state) { return state.Zone.Value; });
    return durable;
}

void ZoneStateStore::Install(std::span<const DurableZoneState> durable)
{
    Zones_.clear();
    for (const DurableZoneState& state : durable)
    {
        ZoneRecord& record = Zones_[state.Zone];
        for (const PersistentEntityId id : state.Destroyed)
            record.Destroyed.emplace(id.Value, ZoneStateForgetting::Never);
        for (const DurableComponentState& component : state.Components)
            record.Components.insert_or_assign(
                ComponentStateKey{ component.Entity.Value, component.Type.Value },
                ComponentRecord{ ZoneStateForgetting::Never, component.Version, component.Bytes });
    }
}
