#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <unordered_set>
#include <utility>

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

const ZoneStateStore::ZoneRecord* ZoneStateStore::Find(ZoneStateScope scope) const
{
    const auto found = Zones_.find(KeyOf(scope));
    return found != Zones_.end() ? &found->second : nullptr;
}

void ZoneStateStore::RecordAuthoredSet(ZoneStateScope scope, std::vector<AuthoredEntityState> authored)
{
    if (!scope.IsValid())
        return;
    ZoneRecord& record = Zones_[KeyOf(scope)];
    record.Authored.clear();
    record.Authored.reserve(authored.size());
    for (AuthoredEntityState& entity : authored)
        if (entity.Id.IsValid())
            record.Authored.insert_or_assign(entity.Id.Value,
                                             AuthoredRecord{ entity.Forgotten, std::move(entity.Baseline) });
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
    for (const auto& [authored, entity] : record.Authored)
        if (!liveSet.contains(authored) && !(detaching && entity.Forgotten == ZoneStateForgetting::AtDetach))
            record.Destroyed.emplace(authored, entity.Forgotten);
    std::erase_if(record.Patches, [&](const auto& entry) {
        return !record.Authored.contains(entry.first) || record.Destroyed.contains(entry.first);
    });
    if (detaching)
    {
        std::erase_if(record.Patches,
                      [](const auto& entry) { return entry.second.Forgotten == ZoneStateForgetting::AtDetach; });
        record.Authored.clear();
    }
}

void ZoneStateStore::RecordPatch(ZoneStateScope scope, PersistentEntityId id, JsonValue patch)
{
    if (!scope.IsValid() || !id.IsValid())
        return;
    if (!patch.IsObject() || patch.Size() == 0)
    {
        if (const auto it = Zones_.find(KeyOf(scope)); it != Zones_.end())
            it->second.Patches.erase(id.Value);
        return;
    }
    const ZoneStateForgetting forgotten = ForgettingOf(scope, id);
    Zones_[KeyOf(scope)].Patches.insert_or_assign(id.Value, PatchRecord{ forgotten, std::move(patch) });
}

void ZoneStateStore::ForgetPatchMember(ZoneStateScope scope, PersistentEntityId id, std::string_view member)
{
    const auto it = Zones_.find(KeyOf(scope));
    if (it == Zones_.end())
        return;
    const auto found = it->second.Patches.find(id.Value);
    if (found == it->second.Patches.end())
        return;
    std::erase_if(found->second.Patch.AsObject(), [&](const auto& entry) { return entry.first == member; });
    if (found->second.Patch.Size() == 0)
        it->second.Patches.erase(found);
}

bool ZoneStateStore::IsRecordedDestroyed(ZoneStateScope scope, PersistentEntityId id) const
{
    const ZoneRecord* record = Find(scope);
    return record != nullptr && record->Destroyed.contains(id.Value);
}

std::size_t ZoneStateStore::RecordedDestroyedCount(ZoneStateScope scope) const
{
    const ZoneRecord* record = Find(scope);
    return record != nullptr ? record->Destroyed.size() : 0;
}

ZoneStateForgetting ZoneStateStore::ForgettingOf(ZoneStateScope scope, PersistentEntityId id) const
{
    const ZoneRecord* record = Find(scope);
    if (record == nullptr)
        return ZoneStateForgetting::Never;
    if (const auto authored = record->Authored.find(id.Value); authored != record->Authored.end())
        return authored->second.Forgotten;
    if (const auto destroyed = record->Destroyed.find(id.Value); destroyed != record->Destroyed.end())
        return destroyed->second;
    if (const auto patched = record->Patches.find(id.Value); patched != record->Patches.end())
        return patched->second.Forgotten;
    return ZoneStateForgetting::Never;
}

const JsonValue* ZoneStateStore::PatchOf(ZoneStateScope scope, PersistentEntityId id) const
{
    const ZoneRecord* record = Find(scope);
    if (record == nullptr)
        return nullptr;
    const auto found = record->Patches.find(id.Value);
    return found != record->Patches.end() ? &found->second.Patch : nullptr;
}

const std::vector<AuthoredComponent>* ZoneStateStore::BaselineOf(ZoneStateScope scope, PersistentEntityId id) const
{
    const ZoneRecord* record = Find(scope);
    if (record == nullptr)
        return nullptr;
    const auto found = record->Authored.find(id.Value);
    return found != record->Authored.end() ? &found->second.Baseline : nullptr;
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
        if (state.Destroyed.empty())
            continue;
        std::ranges::sort(state.Destroyed, {}, &PersistentEntityId::Value);
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
    }
}
