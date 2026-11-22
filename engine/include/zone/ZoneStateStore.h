#pragma once

#include <core/identity/Id.h>
#include <core/json/JsonValue.h>
#include <ecs/ComponentTypeId.h>
#include <world/LevelReference.h>
#include <world/serialization/EntityPatch.h>
#include <zone/ZoneId.h>
#include <zone/ZoneStateLifetime.h>
#include <zone/ZoneStateLists.h>
#include <zone/ZoneStateScope.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

class ComponentSerializerRegistry;

// One zone's durable state, as a save holds it.
struct DurableZoneState
{
    ZoneId Zone;
    // destroyed, changed, and any list this build does not read, as read.
    EncodedZoneLists Lists;
    // Runtime records whose holder is an entity of this zone, as their own encoded
    // list: filled when a save is captured, read when one is restored; the store
    // keeps neither.
    EncodedZoneLists Held;
};

struct DurableLevelState
{
    LevelReference Level;
    std::vector<DurableZoneState> Zones;
};

struct AuthoredEntityState
{
    PersistentEntityId Id;
    ZoneStateForgetting Forgotten = ZoneStateForgetting::Never;
    // What a capture diffs this entity against: by default its Saved components.
    std::vector<AuthoredComponent> Baseline;
};

// World resource SavedGames::KeepLevelState installs; without it a zone keeps destroyed sets only.
struct ZoneStateSerialization
{
    const ComponentSerializerRegistry* Serializers = nullptr;
    SceneSerializationContext* Context = nullptr;
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
    [[nodiscard]] ZoneStateLevelId Level(const LevelReference& level);

    // At import, with every entity the cooked scene authored, those this import
    // suppressed included.
    void RecordAuthoredSet(ZoneStateScope scope, std::vector<AuthoredEntityState> authored);

    // While the zone's entities are alive: authored minus live is destroyed.
    void RecordCapture(ZoneStateScope scope, std::span<const PersistentEntityId> live, ZoneCapture capture);

    // An empty patch forgets the entity's.
    void RecordPatch(ZoneStateScope scope, PersistentEntityId id, JsonValue patch);
    void ForgetPatchMember(ZoneStateScope scope, PersistentEntityId id, std::string_view member);

    [[nodiscard]] bool IsRecordedDestroyed(ZoneStateScope scope, PersistentEntityId id) const;
    [[nodiscard]] std::size_t RecordedDestroyedCount(ZoneStateScope scope) const;
    // Never for an entity this store has no record of.
    [[nodiscard]] ZoneStateForgetting ForgettingOf(ZoneStateScope scope, PersistentEntityId id) const;
    [[nodiscard]] const JsonValue* PatchOf(ZoneStateScope scope, PersistentEntityId id) const;
    // Null unless the zone's current residency authored the entity.
    [[nodiscard]] const std::vector<AuthoredComponent>* BaselineOf(ZoneStateScope scope, PersistentEntityId id) const;

    // Lists this build does not read, and members it does not make, kept as read.
    [[nodiscard]] std::span<const ZoneStateList> CarriedLists(ZoneStateScope scope) const;
    [[nodiscard]] const LevelReference* LevelOf(ZoneStateLevelId level) const;

    // The next import of each of the level's zones replays its cooked scene.
    void ForgetLevel(const LevelReference& level);
    // The same for every level.
    void Clear() { Zones_.clear(); }

    // Every level's never-forgotten state, levels in kind and name order and zones
    // in id order; a zone nothing changed since the last export is the same blob.
    [[nodiscard]] std::vector<DurableLevelState> ExportDurable() const;
    // Replaces everything with `durable`; each zone is read when it next imports.
    void Install(std::span<const DurableLevelState> durable);

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

    struct AuthoredRecord
    {
        ZoneStateForgetting Forgotten = ZoneStateForgetting::Never;
        std::vector<AuthoredComponent> Baseline;
    };

    struct PatchRecord
    {
        ZoneStateForgetting Forgotten = ZoneStateForgetting::Never;
        JsonValue Patch;
    };

    // Parsed while its zone is resident or holds what a save does not keep;
    // otherwise only Encoded.
    struct ZoneRecord
    {
        bool Parsed = true;
        std::unordered_map<std::uint64_t, AuthoredRecord> Authored;
        std::unordered_map<std::uint64_t, ZoneStateForgetting> Destroyed;
        std::unordered_map<std::uint64_t, PatchRecord> Patches;
        std::vector<ZoneStateList> Carried;
        // The never-forgotten part as a save holds it; null when stale or empty.
        EncodedZoneLists Encoded;
    };

    [[nodiscard]] static ScopeKey KeyOf(ZoneStateScope scope) { return { scope.Level.Value, scope.Zone.Value }; }
    [[nodiscard]] ZoneStateLevelId FindLevel(const LevelReference& level) const;
    [[nodiscard]] ZoneRecord* Find(ZoneStateScope scope) const;
    [[nodiscard]] ZoneRecord& Open(ZoneStateScope scope);
    static void Parse(ZoneRecord& record);
    [[nodiscard]] static EncodedZoneLists Encode(const ZoneRecord& record);
    static void Close(ZoneRecord& record);

    std::vector<LevelReference> Levels_;
    mutable std::unordered_map<ScopeKey, ZoneRecord, ScopeKeyHash> Zones_;
};
