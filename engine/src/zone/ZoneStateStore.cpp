#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <unordered_set>

ZoneStateLevelId ZoneStateStore::FindLevel(const LevelReference& level) const
{
    const auto found = std::ranges::find(Levels_, level);
    return found != Levels_.end() ? ZoneStateLevelId{ static_cast<std::uint32_t>(found - Levels_.begin()) + 1 }
                                  : ZoneStateLevelId{};
}

ZoneStateScope ZoneStateStore::Scope(const LevelReference& level, ZoneId zone)
{
    ZoneStateLevelId id = FindLevel(level);
    if (!id.IsValid())
    {
        Levels_.push_back(level);
        id = ZoneStateLevelId{ static_cast<std::uint32_t>(Levels_.size()) };
    }
    return ZoneStateScope{ id, zone };
}

void ZoneStateStore::RecordAuthoredSet(ZoneStateScope scope, std::span<const AuthoredEntityState> authored)
{
    if (!scope.IsValid())
        return;
    ZoneRecord& record = Zones_[KeyOf(scope)];
    record.Authored.clear();
    record.Authored.reserve(authored.size());
    for (const AuthoredEntityState& entity : authored)
        if (entity.Id.IsValid())
            record.Authored.insert_or_assign(entity.Id.Value, entity.Forgotten);
}

void ZoneStateStore::RecordCapture(ZoneStateScope scope, std::span<const PersistentEntityId> live,
                                   ZoneCapture capture)
{
    const auto it = Zones_.find(KeyOf(scope));
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

bool ZoneStateStore::IsRecordedDestroyed(ZoneStateScope scope, PersistentEntityId id) const
{
    const auto it = Zones_.find(KeyOf(scope));
    return it != Zones_.end() && it->second.Destroyed.contains(id.Value);
}

std::size_t ZoneStateStore::RecordedDestroyedCount(ZoneStateScope scope) const
{
    const auto it = Zones_.find(KeyOf(scope));
    return it != Zones_.end() ? it->second.Destroyed.size() : 0;
}

ZoneStateForgetting ZoneStateStore::ForgettingOf(ZoneStateScope scope, PersistentEntityId id) const
{
    const auto it = Zones_.find(KeyOf(scope));
    if (it == Zones_.end())
        return ZoneStateForgetting::Never;
    if (const auto authored = it->second.Authored.find(id.Value); authored != it->second.Authored.end())
        return authored->second;
    if (const auto destroyed = it->second.Destroyed.find(id.Value); destroyed != it->second.Destroyed.end())
        return destroyed->second;
    return ZoneStateForgetting::Never;
}

void ZoneStateStore::RecordComponentState(ZoneStateScope scope, PersistentEntityId id, ComponentTypeId type,
                                          std::uint16_t version, std::span<const std::byte> state)
{
    if (!scope.IsValid())
        return;
    const ZoneStateForgetting forgotten = ForgettingOf(scope, id);
    ComponentRecord& record = Zones_[KeyOf(scope)].Components[ComponentStateKey{ id.Value, type.Value }];
    record.Forgotten = forgotten;
    record.Version = version;
    record.Bytes.assign(state.begin(), state.end());
}

void ZoneStateStore::ForgetComponentState(ZoneStateScope scope, PersistentEntityId id, ComponentTypeId type)
{
    if (const auto it = Zones_.find(KeyOf(scope)); it != Zones_.end())
        it->second.Components.erase(ComponentStateKey{ id.Value, type.Value });
}

RememberedComponentState ZoneStateStore::ComponentState(ZoneStateScope scope, PersistentEntityId id,
                                                        ComponentTypeId type) const
{
    const auto it = Zones_.find(KeyOf(scope));
    if (it == Zones_.end())
        return {};
    const auto found = it->second.Components.find(ComponentStateKey{ id.Value, type.Value });
    if (found == it->second.Components.end())
        return {};
    return RememberedComponentState{ found->second.Version, found->second.Bytes };
}

std::vector<DurableZoneState> ZoneStateStore::ExportDurable(const LevelReference& level) const
{
    std::vector<DurableZoneState> durable;
    const ZoneStateLevelId levelId = FindLevel(level);
    for (const auto& [key, record] : Zones_)
    {
        if (key.Level != levelId.Value)
            continue;
        DurableZoneState state;
        state.Zone = ZoneId{ key.Zone };
        for (const auto& [id, forgotten] : record.Destroyed)
            if (forgotten == ZoneStateForgetting::Never)
                state.Destroyed.push_back(PersistentEntityId{ id });
        for (const auto& [componentKey, component] : record.Components)
            if (component.Forgotten == ZoneStateForgetting::Never)
                state.Components.push_back(DurableComponentState{ .Entity = PersistentEntityId{ componentKey.Entity },
                                                                  .Type = ComponentTypeId{ componentKey.Type },
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

void ZoneStateStore::Install(const LevelReference& level, std::span<const DurableZoneState> durable)
{
    Zones_.clear();
    for (const DurableZoneState& state : durable)
    {
        ZoneRecord& record = Zones_[KeyOf(Scope(level, state.Zone))];
        for (const PersistentEntityId id : state.Destroyed)
            record.Destroyed.emplace(id.Value, ZoneStateForgetting::Never);
        for (const DurableComponentState& component : state.Components)
            record.Components.insert_or_assign(
                ComponentStateKey{ component.Entity.Value, component.Type.Value },
                ComponentRecord{ ZoneStateForgetting::Never, component.Version, component.Bytes });
    }
}
