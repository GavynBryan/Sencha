#pragma once

#include <core/identity/Id.h>
#include <zone/ZoneId.h>

#include <cstddef>
#include <span>
#include <unordered_map>
#include <unordered_set>

//=============================================================================
// ZoneStateStore
//
// In-session memory of how each zone's live state deviates from its cooked
// scene, keyed by persistent entity identity. The zone import path records the
// authored id set and suppresses recorded-destroyed entities on re-import; the
// detach path captures the deviation by diffing live ids against the authored
// set. Unload therefore stops meaning forget: a pickup taken before a zone
// streamed out is still gone when it streams back in.
//
// This is the runtime half of the zone state overlay. Serializing records for
// save files and capturing changed component fields extend this store; they do
// not replace it.
//=============================================================================
class ZoneStateStore
{
public:
    // Called on successful zone import with every persistent id the cooked
    // scene authored, before suppression. The set is a property of the cooked
    // artifact, so re-recording on each import is idempotent while staying
    // correct across a recook that added or removed entities.
    void RecordAuthoredSet(ZoneId zone, std::span<const PersistentEntityId> authored)
    {
        ZoneStateRecord& record = Zones_[zone];
        record.Authored.clear();
        record.Authored.reserve(authored.size());
        for (const PersistentEntityId id : authored)
            if (id.IsValid())
                record.Authored.insert(id.Value);
    }

    // Called at detach while the zone's entities are still alive. Destroyed is
    // recomputed as authored-minus-live: entities suppressed on import are not
    // live either, so accumulated destruction survives any number of
    // unload/reload cycles without a union step.
    void RecordDetachCapture(ZoneId zone, std::span<const PersistentEntityId> live)
    {
        const auto it = Zones_.find(zone);
        if (it == Zones_.end())
            return;

        std::unordered_set<std::uint64_t> liveSet;
        liveSet.reserve(live.size());
        for (const PersistentEntityId id : live)
            liveSet.insert(id.Value);

        ZoneStateRecord& record = it->second;
        record.Destroyed.clear();
        for (const std::uint64_t authored : record.Authored)
            if (!liveSet.contains(authored))
                record.Destroyed.insert(authored);
    }

    [[nodiscard]] bool IsRecordedDestroyed(ZoneId zone, PersistentEntityId id) const
    {
        const auto it = Zones_.find(zone);
        return it != Zones_.end() && it->second.Destroyed.contains(id.Value);
    }

    [[nodiscard]] std::size_t RecordedDestroyedCount(ZoneId zone) const
    {
        const auto it = Zones_.find(zone);
        return it != Zones_.end() ? it->second.Destroyed.size() : 0;
    }

    // Reset-to-authored: the next import of every zone replays the cooked
    // scene unmodified.
    void Clear() { Zones_.clear(); }

private:
    struct ZoneStateRecord
    {
        std::unordered_set<std::uint64_t> Authored;
        std::unordered_set<std::uint64_t> Destroyed;
    };

    std::unordered_map<ZoneId, ZoneStateRecord> Zones_;
};
