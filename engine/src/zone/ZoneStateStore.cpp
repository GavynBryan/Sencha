#include <zone/ZoneStateStore.h>

#include <core/json/JsonEquals.h>

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace
{
    void SortGained(std::vector<GainedEntityState>& gained)
    {
        std::ranges::stable_sort(gained, [](const GainedEntityState& a, const GainedEntityState& b) {
            return a.Id.IsValid() != b.Id.IsValid() ? !a.Id.IsValid() : a.Id.Value < b.Id.Value;
        });
    }
}

ZoneStateLevelId ZoneStateStore::FindLevel(const LevelReference& level) const
{
    const auto found = std::ranges::find(Levels_, level);
    return found != Levels_.end() ? ZoneStateLevelId{ static_cast<std::uint32_t>(found - Levels_.begin()) + 1 }
                                  : ZoneStateLevelId{};
}

ZoneStateLevelId ZoneStateStore::Level(const LevelReference& level)
{
    ZoneStateLevelId id = FindLevel(level);
    if (!id.IsValid())
    {
        Levels_.push_back(level);
        id = ZoneStateLevelId{ static_cast<std::uint32_t>(Levels_.size()) };
    }
    return id;
}

ZoneStateScope ZoneStateStore::Scope(const LevelReference& level, ZoneId zone)
{
    return ZoneStateScope{ Level(level), zone };
}

const LevelReference* ZoneStateStore::LevelOf(ZoneStateLevelId level) const
{
    return level.IsValid() && level.Value <= Levels_.size() ? &Levels_[level.Value - 1] : nullptr;
}

void ZoneStateStore::Parse(ZoneRecord& record)
{
    if (record.Parsed)
        return;
    record.Parsed = true;
    if (record.Encoded == nullptr)
        return;
    std::optional<std::vector<ZoneStateList>> lists = DecodeZoneLists(*record.Encoded);
    if (!lists.has_value())
        return;
    for (ZoneStateList& list : *lists)
    {
        if (list.Name == kDestroyedZoneList && list.Version <= kZoneListVersion)
        {
            if (const std::optional<std::vector<PersistentEntityId>> ids = DecodeDestroyedList(list.Payload))
            {
                for (const PersistentEntityId id : *ids)
                    record.Destroyed.emplace(id.Value, ZoneStateForgetting::Never);
                continue;
            }
        }
        else if (list.Name == kChangedZoneList && list.Version <= kZoneListVersion)
        {
            if (std::optional<std::vector<ZoneListEntity>> changed = DecodeEntityList(list.Payload))
            {
                for (ZoneListEntity& entity : *changed)
                    record.Patches.insert_or_assign(entity.Id.Value,
                                                    PatchRecord{ ZoneStateForgetting::Never, std::move(entity.Value) });
                continue;
            }
        }
        else if (list.Name == kGainedZoneList && list.Version <= kZoneListVersion)
        {
            if (std::optional<std::vector<GainedZoneMember>> members = DecodeGainedList(list.Payload))
            {
                for (GainedZoneMember& member : *members)
                {
                    const std::optional<PersistentEntityId> id =
                        member.Form == kWholeGainedForm ? WholeGainedMemberId(member.Payload) : std::nullopt;
                    record.Gained.push_back(GainedEntityState{ id.value_or(PersistentEntityId{}),
                                                               ZoneStateForgetting::Never, std::move(member) });
                }
                SortGained(record.Gained);
                continue;
            }
        }
        record.Carried.push_back(std::move(list));
    }
}

EncodedZoneLists ZoneStateStore::Encode(const ZoneRecord& record)
{
    std::vector<PersistentEntityId> destroyed;
    for (const auto& [id, forgotten] : record.Destroyed)
        if (forgotten == ZoneStateForgetting::Never)
            destroyed.push_back(PersistentEntityId{ id });
    std::vector<ZoneListEntity> changed;
    for (const auto& [id, patched] : record.Patches)
        if (patched.Forgotten == ZoneStateForgetting::Never)
            changed.push_back(ZoneListEntity{ PersistentEntityId{ id }, patched.Patch });
    std::vector<GainedZoneMember> gained;
    for (const GainedEntityState& member : record.Gained)
        if (member.Forgotten == ZoneStateForgetting::Never)
            gained.push_back(member.Member);
    if (destroyed.empty() && changed.empty() && gained.empty() && record.Carried.empty())
        return nullptr;
    std::ranges::sort(destroyed, {}, &PersistentEntityId::Value);
    std::ranges::sort(changed, {}, [](const ZoneListEntity& entity) { return entity.Id.Value; });
    std::vector<ZoneStateList> lists;
    if (!destroyed.empty())
        lists.push_back({ std::string(kDestroyedZoneList), kZoneListVersion, EncodeDestroyedList(destroyed) });
    if (!changed.empty())
        lists.push_back({ std::string(kChangedZoneList), kZoneListVersion, EncodeEntityList(changed) });
    if (!gained.empty())
        lists.push_back({ std::string(kGainedZoneList), kZoneListVersion, EncodeGainedList(gained) });
    lists.insert(lists.end(), record.Carried.begin(), record.Carried.end());
    return EncodeZoneLists(lists);
}

void ZoneStateStore::Close(ZoneRecord& record)
{
    const bool sessionOnly =
        std::ranges::any_of(record.Destroyed, [](const auto& entry) { return entry.second != ZoneStateForgetting::Never; })
        || std::ranges::any_of(record.Patches,
                               [](const auto& entry) { return entry.second.Forgotten != ZoneStateForgetting::Never; })
        || std::ranges::any_of(record.Gained,
                               [](const GainedEntityState& member) { return member.Forgotten != ZoneStateForgetting::Never; });
    if (sessionOnly)
        return;
    if (record.Encoded == nullptr)
        record.Encoded = Encode(record);
    record.Destroyed.clear();
    record.Patches.clear();
    record.Gained.clear();
    record.Carried.clear();
    record.Parsed = false;
}

ZoneStateStore::ZoneRecord* ZoneStateStore::Find(ZoneStateScope scope) const
{
    const auto found = Zones_.find(KeyOf(scope));
    if (found == Zones_.end())
        return nullptr;
    Parse(found->second);
    return &found->second;
}

ZoneStateStore::ZoneRecord& ZoneStateStore::Open(ZoneStateScope scope)
{
    ZoneRecord& record = Zones_[KeyOf(scope)];
    Parse(record);
    return record;
}

void ZoneStateStore::RecordAuthoredSet(ZoneStateScope scope, std::vector<AuthoredEntityState> authored)
{
    if (!scope.IsValid())
        return;
    ZoneRecord& record = Open(scope);
    record.Authored.clear();
    record.Authored.reserve(authored.size());
    for (AuthoredEntityState& entity : authored)
        if (entity.Id.IsValid())
            record.Authored.insert_or_assign(entity.Id.Value,
                                             AuthoredRecord{ entity.Forgotten, std::move(entity.Baseline) });
}

void ZoneStateStore::RecordDestroyed(ZoneRecord& record, std::span<const PersistentEntityId> live, bool detaching)
{
    std::unordered_set<std::uint64_t> liveSet;
    liveSet.reserve(live.size());
    for (const PersistentEntityId id : live)
        liveSet.insert(id.Value);

    std::unordered_map<std::uint64_t, ZoneStateForgetting> destroyed;
    for (const auto& [authored, entity] : record.Authored)
        if (!liveSet.contains(authored) && !(detaching && entity.Forgotten == ZoneStateForgetting::AtDetach))
            destroyed.emplace(authored, entity.Forgotten);
    if (destroyed != record.Destroyed)
    {
        record.Destroyed = std::move(destroyed);
        record.Encoded = nullptr;
    }
    const std::size_t erased = std::erase_if(record.Patches, [&](const auto& entry) {
        return !record.Authored.contains(entry.first) || record.Destroyed.contains(entry.first)
            || (detaching && entry.second.Forgotten == ZoneStateForgetting::AtDetach);
    });
    if (erased > 0)
        record.Encoded = nullptr;
}

void ZoneStateStore::RecordCapture(ZoneStateScope scope, std::span<const PersistentEntityId> live,
                                   ZoneCapture capture)
{
    ZoneRecord* found = Find(scope);
    if (found == nullptr)
        return;
    ZoneRecord& record = *found;
    const bool detaching = capture == ZoneCapture::Detach;
    if (!record.Authored.empty())
        RecordDestroyed(record, live, detaching);
    if (detaching)
    {
        record.Authored.clear();
        Close(record);
        if (!record.Parsed && record.Encoded == nullptr)
            Zones_.erase(KeyOf(scope));
    }
}

void ZoneStateStore::RecordPatch(ZoneStateScope scope, PersistentEntityId id, JsonValue patch)
{
    if (!scope.IsValid() || !id.IsValid())
        return;
    if (!patch.IsObject() || patch.Size() == 0)
    {
        if (ZoneRecord* record = Find(scope); record != nullptr && record->Patches.erase(id.Value) > 0)
            record->Encoded = nullptr;
        return;
    }
    const ZoneStateForgetting forgotten = ForgettingOf(scope, id);
    ZoneRecord& record = Open(scope);
    const auto existing = record.Patches.find(id.Value);
    if (existing != record.Patches.end() && existing->second.Forgotten == forgotten
        && JsonEquals(existing->second.Patch, patch))
        return;
    record.Patches.insert_or_assign(id.Value, PatchRecord{ forgotten, std::move(patch) });
    record.Encoded = nullptr;
}

void ZoneStateStore::ForgetPatchMember(ZoneStateScope scope, PersistentEntityId id, std::string_view member)
{
    ZoneRecord* record = Find(scope);
    if (record == nullptr)
        return;
    const auto found = record->Patches.find(id.Value);
    if (found == record->Patches.end())
        return;
    std::erase_if(found->second.Patch.AsObject(), [&](const auto& entry) { return entry.first == member; });
    if (found->second.Patch.Size() == 0)
        record->Patches.erase(found);
    record->Encoded = nullptr;
}

void ZoneStateStore::RecordGained(ZoneStateScope scope, std::vector<GainedEntityState> gained)
{
    if (!scope.IsValid())
        return;
    ZoneRecord* found = Find(scope);
    if (found == nullptr && gained.empty())
        return;
    ZoneRecord& record = found != nullptr ? *found : Open(scope);
    std::vector<GainedEntityState> members;
    for (const GainedEntityState& member : record.Gained)
        if (!member.Id.IsValid())
            members.push_back(member);
    for (GainedEntityState& member : gained)
        if (member.Id.IsValid())
            members.push_back(std::move(member));
    SortGained(members);
    if (members == record.Gained)
        return;
    record.Gained = std::move(members);
    record.Encoded = nullptr;
}

void ZoneStateStore::ForgetGained(ZoneStateScope scope, PersistentEntityId id)
{
    if (ZoneRecord* record = Find(scope); record != nullptr && id.IsValid()
        && std::erase_if(record->Gained, [&](const GainedEntityState& member) { return member.Id == id; }) > 0)
        record->Encoded = nullptr;
}

std::span<const GainedEntityState> ZoneStateStore::GainedOf(ZoneStateScope scope) const
{
    const ZoneRecord* record = Find(scope);
    return record != nullptr ? std::span<const GainedEntityState>(record->Gained) : std::span<const GainedEntityState>{};
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
    if (const auto gained = std::ranges::find(record->Gained, id, &GainedEntityState::Id); gained != record->Gained.end())
        return gained->Forgotten;
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
    return found != record->Authored.end() && found->second.Forgotten != ZoneStateForgetting::AtDetach
        ? &found->second.Baseline
        : nullptr;
}

void ZoneStateStore::ForgetLevel(const LevelReference& level)
{
    const ZoneStateLevelId id = FindLevel(level);
    std::erase_if(Zones_, [&](const auto& entry) { return entry.first.Level == id.Value; });
}

std::vector<DurableLevelState> ZoneStateStore::ExportDurable() const
{
    std::vector<std::pair<ScopeKey, EncodedZoneLists>> zones;
    for (auto& [key, record] : Zones_)
    {
        if (record.Parsed && record.Encoded == nullptr)
            record.Encoded = Encode(record);
        if (record.Encoded != nullptr)
            zones.emplace_back(key, record.Encoded);
    }
    const auto levelOrder = [&](std::uint32_t a, std::uint32_t b) {
        const LevelReference& left = Levels_[a - 1];
        const LevelReference& right = Levels_[b - 1];
        return left.Kind != right.Kind ? left.Kind < right.Kind : left.Name < right.Name;
    };
    std::ranges::sort(zones, [&](const auto& a, const auto& b) {
        if (a.first.Level != b.first.Level)
            return levelOrder(a.first.Level, b.first.Level);
        return a.first.Zone < b.first.Zone;
    });
    std::vector<DurableLevelState> durable;
    for (auto& [key, lists] : zones)
    {
        if (durable.empty() || durable.back().Level != Levels_[key.Level - 1])
            durable.push_back(DurableLevelState{ Levels_[key.Level - 1], {} });
        durable.back().Zones.push_back(DurableZoneState{ ZoneId{ key.Zone }, std::move(lists), nullptr });
    }
    return durable;
}

void ZoneStateStore::Install(std::span<const DurableLevelState> durable)
{
    Zones_.clear();
    for (const DurableLevelState& level : durable)
        for (const DurableZoneState& zone : level.Zones)
            if (zone.Lists != nullptr)
            {
                ZoneRecord& record = Zones_[KeyOf(Scope(level.Level, zone.Zone))];
                record = ZoneRecord{};
                record.Parsed = false;
                record.Encoded = zone.Lists;
            }
}
