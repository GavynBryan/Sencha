#pragma once

#include <core/identity/Id.h>
#include <ecs/ComponentTypeId.h>
#include <world/LevelReference.h>
#include <zone/ZoneId.h>
#include <zone/ZoneStateLifetime.h>
#include <zone/ZoneStateScope.h>

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

// How each zone of each level deviates from its cooked scene, keyed by
// persistent identity: docs/plans/runtime-stable-identity.md.
class ZoneStateStore
{
public:
    [[nodiscard]] ZoneStateScope Scope(const LevelReference& level, ZoneId zone);

    // At import, with every entity the cooked scene authored, those this import
    // suppressed included.
    void RecordAuthoredSet(ZoneStateScope scope, std::span<const AuthoredEntityState> authored);

    // While the zone's entities are alive: authored minus live is destroyed.
    void RecordCapture(ZoneStateScope scope, std::span<const PersistentEntityId> live, ZoneCapture capture);

    [[nodiscard]] bool IsRecordedDestroyed(ZoneStateScope scope, PersistentEntityId id) const;
    [[nodiscard]] std::size_t RecordedDestroyedCount(ZoneStateScope scope) const;
    // Never for an entity this store has no record of.
    [[nodiscard]] ZoneStateForgetting ForgettingOf(ZoneStateScope scope, PersistentEntityId id) const;

    void RecordComponentState(ZoneStateScope scope, PersistentEntityId id, ComponentTypeId type,
                              std::uint16_t version, std::span<const std::byte> state);
    void ForgetComponentState(ZoneStateScope scope, PersistentEntityId id, ComponentTypeId type);
    // Empty bytes when nothing was recorded.
    [[nodiscard]] RememberedComponentState ComponentState(ZoneStateScope scope, PersistentEntityId id,
                                                          ComponentTypeId type) const;

    // Reset-to-authored: the next import of every zone replays the cooked scene.
    void Clear() { Zones_.clear(); }

    [[nodiscard]] std::vector<DurableZoneState> ExportDurable(const LevelReference& level) const;
    // Replaces everything with `durable`, as `level`'s.
    void Install(const LevelReference& level, std::span<const DurableZoneState> durable);

private:
    struct ScopeKey
    {
        std::uint32_t Level = 0;
        std::uint64_t Zone = 0;
        friend bool operator==(const ScopeKey&, const ScopeKey&) = default;
    };

    struct ScopeKeyHash
    {
        std::size_t operator()(const ScopeKey& key) const
        {
            return static_cast<std::size_t>(key.Zone * 1099511628211ull ^ key.Level);
        }
    };

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

    [[nodiscard]] static ScopeKey KeyOf(ZoneStateScope scope) { return { scope.Level.Value, scope.Zone.Value }; }
    [[nodiscard]] ZoneStateLevelId FindLevel(const LevelReference& level) const;

    std::vector<LevelReference> Levels_;
    std::unordered_map<ScopeKey, ZoneRecord, ScopeKeyHash> Zones_;
};
