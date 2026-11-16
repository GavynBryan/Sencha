#pragma once

#include <core/identity/Id.h>
#include <ecs/ComponentTypeId.h>
#include <zone/ZoneId.h>
#include <zone/ZoneStateLifetime.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

struct DurableComponentState
{
    PersistentEntityId Entity;
    ComponentTypeId Type;
    std::uint16_t Version = 0;
    std::vector<std::byte> Bytes;

    friend bool operator==(const DurableComponentState&, const DurableComponentState&) = default;
};

// What a zone never forgets, in id order: the part a save keeps.
struct DurableZoneState
{
    ZoneId Zone;
    std::vector<PersistentEntityId> Destroyed;
    std::vector<DurableComponentState> Components;

    friend bool operator==(const DurableZoneState&, const DurableZoneState&) = default;
};

struct AuthoredEntityState
{
    PersistentEntityId Id;
    ZoneStateForgetting Forgotten = ZoneStateForgetting::Never;
};

struct RememberedComponentState
{
    std::uint16_t Version = 0;
    std::span<const std::byte> Bytes;
};

enum class ZoneCapture : std::uint8_t
{
    // The zone stays; what it authored stays known.
    Live,
    // The zone is leaving: what is forgotten at detach goes, and so does the
    // authored set, which the next import restates.
    Detach,
};

// How each zone's live state deviates from its cooked scene, keyed by
// persistent identity: docs/plans/runtime-stable-identity.md.
class ZoneStateStore
{
public:
    // At import, with every entity the cooked scene authored, those this import
    // suppressed included.
    void RecordAuthoredSet(ZoneId zone, std::span<const AuthoredEntityState> authored);

    // While the zone's entities are alive: authored minus live is destroyed.
    void RecordCapture(ZoneId zone, std::span<const PersistentEntityId> live, ZoneCapture capture);

    [[nodiscard]] bool IsRecordedDestroyed(ZoneId zone, PersistentEntityId id) const;
    [[nodiscard]] std::size_t RecordedDestroyedCount(ZoneId zone) const;
    // Never for an entity this store has no record of.
    [[nodiscard]] ZoneStateForgetting ForgettingOf(ZoneId zone, PersistentEntityId id) const;

    void RecordComponentState(ZoneId zone, PersistentEntityId id, ComponentTypeId type, std::uint16_t version,
                              std::span<const std::byte> state);
    void ForgetComponentState(ZoneId zone, PersistentEntityId id, ComponentTypeId type);
    // Empty bytes when nothing was recorded.
    [[nodiscard]] RememberedComponentState ComponentState(ZoneId zone, PersistentEntityId id,
                                                          ComponentTypeId type) const;

    // Reset-to-authored: the next import of every zone replays the cooked scene.
    void Clear() { Zones_.clear(); }

    [[nodiscard]] std::vector<DurableZoneState> ExportDurable() const;
    // Replaces everything with `durable`.
    void Install(std::span<const DurableZoneState> durable);

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

    struct ComponentRecord
    {
        ZoneStateForgetting Forgotten = ZoneStateForgetting::Never;
        std::uint16_t Version = 0;
        std::vector<std::byte> Bytes;
    };

    struct ZoneRecord
    {
        std::unordered_map<std::uint64_t, ZoneStateForgetting> Authored;
        std::unordered_map<std::uint64_t, ZoneStateForgetting> Destroyed;
        std::unordered_map<ComponentStateKey, ComponentRecord, ComponentStateKeyHash> Components;
    };

    std::unordered_map<ZoneId, ZoneRecord> Zones_;
};
