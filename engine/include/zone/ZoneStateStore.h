#pragma once

#include <core/identity/Id.h>
#include <ecs/ComponentTypeId.h>
#include <zone/ZoneId.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
// What survives between residencies is the deviation, not the baseline: the
// authored set lives only for the span of one residency because every import
// restates it from the cooked artifact. Retained memory therefore tracks how
// much state has actually changed, not how much of the world has been visited.
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

        ZoneStateRecord& record = it->second;
        // Without a baseline there is nothing to diff against, and recomputing
        // from an empty one would read as "everything survived" and erase the
        // deviation already recorded. Only an import can supply a baseline.
        if (record.Authored.empty())
            return;

        std::unordered_set<std::uint64_t> liveSet;
        liveSet.reserve(live.size());
        for (const PersistentEntityId id : live)
            liveSet.insert(id.Value);

        record.Destroyed.clear();
        for (const std::uint64_t authored : record.Authored)
            if (!liveSet.contains(authored))
                record.Destroyed.insert(authored);

        // The baseline is a property of the cooked artifact, restated by every
        // import, so holding it between residencies would make this store grow
        // with zones visited rather than with state that actually deviates.
        record.Authored.clear();
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

    // State a system captured for one component of one entity as its zone
    // detached, in whatever form that system reads back when the zone next
    // attaches. Like the destroyed set it is a deviation: the system forgets a
    // record once the entity is back as authored, so memory follows change.
    void RecordComponentState(ZoneId zone, PersistentEntityId id, ComponentTypeId type,
                              std::span<const std::byte> state)
    {
        std::vector<std::byte>& record = Zones_[zone].Components[ComponentStateKey{ id.Value, type.Value }];
        record.assign(state.begin(), state.end());
    }

    void ForgetComponentState(ZoneId zone, PersistentEntityId id, ComponentTypeId type)
    {
        if (const auto it = Zones_.find(zone); it != Zones_.end())
            it->second.Components.erase(ComponentStateKey{ id.Value, type.Value });
    }

    // Empty when nothing was recorded.
    [[nodiscard]] std::span<const std::byte> ComponentState(ZoneId zone, PersistentEntityId id,
                                                            ComponentTypeId type) const
    {
        const auto it = Zones_.find(zone);
        if (it == Zones_.end())
            return {};
        const auto found = it->second.Components.find(ComponentStateKey{ id.Value, type.Value });
        return found != it->second.Components.end() ? std::span<const std::byte>(found->second)
                                                    : std::span<const std::byte>();
    }

    // Reset-to-authored: the next import of every zone replays the cooked
    // scene unmodified.
    void Clear() { Zones_.clear(); }

private:
    struct ComponentStateKey
    {
        std::uint64_t Entity = 0;
        std::uint64_t Type = 0;
        friend bool operator==(const ComponentStateKey&, const ComponentStateKey&) = default;
    };

    struct ComponentStateKeyHash
    {
        std::size_t operator()(const ComponentStateKey& key) const
        {
            return static_cast<std::size_t>(key.Entity * 1099511628211ull ^ key.Type);
        }
    };

    struct ZoneStateRecord
    {
        std::unordered_set<std::uint64_t> Authored;
        std::unordered_set<std::uint64_t> Destroyed;
        std::unordered_map<ComponentStateKey, std::vector<std::byte>, ComponentStateKeyHash> Components;
    };

    std::unordered_map<ZoneId, ZoneStateRecord> Zones_;
};
