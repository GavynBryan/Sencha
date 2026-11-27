#pragma once

#include <core/containers/ListPool.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <equipment/EquipmentLayout.h>
#include <inventory/ItemDefinition.h>
#include <world/identity/PersistableRef.h>

#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

class World;

inline constexpr std::size_t kEquipmentRecordCapacity = kMaxEquipmentSlots;

enum class EquipmentDerivation : std::uint8_t
{
    Pending,
    Maintained,
    RefusedForRoom,
    RefusedByContent,
};

struct EquipmentRecord
{
    std::uint32_t Key = 0;
    ItemId Definition;
    std::uint32_t Slots = 0;
    std::uint8_t Primary = kNoEquipmentSlot;
    PersistableRef Source;
    EquipmentDerivation Derivation = EquipmentDerivation::Pending;

    friend bool operator==(const EquipmentRecord&, const EquipmentRecord&) = default;
};

using EquipmentRecordLists = ListPool<struct EquipmentRecordListTag, EquipmentRecord>;

struct SENCHA_COMPONENT("sencha.equipment_state") SENCHA_SAVED EquipmentState
{
    EquipmentLayoutId Layout;
    std::uint8_t Count = 0;
    std::uint32_t NextKey = 1;
    EquipmentRecordLists::HandleType Records;
};

static_assert(std::is_trivially_copyable_v<EquipmentState>, "equipment components live in ECS chunks");

template <>
struct ComponentTraits<EquipmentState>
{
    static void OnAdd(EquipmentState& state, World& world, EntityId wearer);
    static void OnRemove(const EquipmentState& state, World& world, EntityId wearer);
};

struct EquipmentRecordList
{
    EquipmentLayoutId Layout;
    std::uint32_t NextKey = 1;
    std::vector<EquipmentRecord> Records;

    friend bool operator==(const EquipmentRecordList&, const EquipmentRecordList&) = default;
};

[[nodiscard]] std::span<const EquipmentRecord> EquipmentRecordsOf(const World& world, const EquipmentState& state);
[[nodiscard]] std::span<const EquipmentRecord> EquipmentRecordsOf(const World& world, EntityId wearer);
[[nodiscard]] std::span<EquipmentRecord> EditEquipmentRecords(World& world, EntityId wearer);
void RemoveEquipmentRecord(World& world, EntityId wearer, std::size_t index);

inline constexpr std::size_t kNoEquipmentRecord = static_cast<std::size_t>(-1);
[[nodiscard]] std::size_t FindEquipmentRecordHolding(std::span<const EquipmentRecord> records, std::uint8_t slot);

std::uint32_t WearRecord(std::vector<EquipmentRecord>& records, std::uint32_t& nextKey, EquipmentRecord record);
std::uint32_t WearEquipmentRecord(World& world, EntityId wearer, const EquipmentRecord& record);

[[nodiscard]] EquipmentRecordList CopyEquipment(const World& world, EntityId wearer);
[[nodiscard]] EquipmentState MakeEquipmentState(World& world, const EquipmentRecordList& worn);
void SetEquipment(World& world, EntityId wearer, const EquipmentRecordList& worn);

#if !defined(SENCHA_CODEGEN)
#  include <equipment/EquipmentComponents.sencha.h>
#  include <equipment/EquipmentStateWire.h>
#endif
