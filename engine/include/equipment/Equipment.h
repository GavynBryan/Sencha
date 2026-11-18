#pragma once

#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>
#include <equipment/EquipmentLayout.h>
#include <inventory/Inventory.h>
#include <time/FrameClock.h>

#include <cstdint>
#include <string_view>

class World;
struct EquipmentRecord;
struct EquipmentState;

// docs/gameplay/equipment.md, "Equipping".
enum class EquipmentRefusal : std::uint8_t
{
    None,
    NotAuthority,
    Missing,
    NotPermitted,
    NotEquippable,
    AlreadyEquipped,
    NoSlot,
    NotAccepted,
    Locked,
    Full,
    GrantRoom,
    NotMaintained,
    Undeclared,
};

struct EquipmentResult
{
    EquipmentRefusal Refusal = EquipmentRefusal::None;
    std::uint32_t Record = 0;
};

// Owner thread, outside any query, in the tick `now`.
class Equipment
{
public:
    Equipment(World& world, const FixedSimTime& now)
        : Entities(&world)
        , Now(now)
    {
    }

    EquipmentResult Equip(EntityId wearer, EntityId entry, std::string_view slot = {});
    EquipmentResult Unequip(EntityId wearer, std::string_view slot, ItemChangeClass change = ItemChangeClass::Requested);
    EquipmentResult Cycle(EntityId wearer, std::string_view group);

private:
    World* Entities;
    FixedSimTime Now;
};

// For each name the item occupies: the slot asked for (the first name only),
// else the first free one, else the first; no two names take one slot.
[[nodiscard]] EquipmentRefusal ResolveEquipmentSlots(const World& world, const EquipmentLayout& layout,
                                                     const EquipmentState& state, const ItemDefinition& item,
                                                     std::string_view asked, std::uint32_t& mask, std::uint8_t& primary);

[[nodiscard]] bool EquipmentWearerLocked(const World& world, EntityId wearer);
// The wearer's lock, or the record's own: its source holds one of its item's `equip.lock_tags`.
[[nodiscard]] bool EquipmentRecordLocked(const World& world, EntityId wearer, const EquipmentRecord& record);

void EndEquipmentRecord(World& world, EntityId wearer, std::size_t index);
void EndMaintained(World& world, const EquipmentRecord& record);

[[nodiscard]] LeaseOwner EquipmentRecordOwner(EntityId source, std::uint32_t record);

// Authority only; idempotent. Returns the records refused.
std::size_t ReconcileEquipment(World& world, EntityId wearer, const FixedSimTime& now);
void DeriveDueEquipmentRecords(World& world, EntityId wearer, const FixedSimTime& now);

EquipmentLayoutId RedefineEquipmentLayout(World& world, std::string_view name, EquipmentLayout layout);
