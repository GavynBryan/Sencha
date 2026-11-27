#pragma once

#include <ecs/ComponentAnnotations.h>
#include <equipment/EquipmentLayout.h>
#include <inventory/ItemDefinition.h>
#include <world/identity/PersistableRef.h>

#include <cstdint>
#include <type_traits>

// docs/gameplay/equipment.md, "Records".
inline constexpr std::size_t kEquipmentRecordCapacity = 16;

// Never saved or sent: a loaded, authored or replicated record is Pending.
enum class EquipmentDerivation : std::uint8_t
{
    Pending,
    Maintained,
    RefusedForRoom,
    RefusedByContent,
};

struct EquipmentRecord
{
    // Unique on its wearer and never reused: what its effects are owned by.
    std::uint32_t Key = 0;
    ItemId Definition;
    std::uint32_t Slots = 0;
    std::uint8_t Primary = kNoEquipmentSlot;
    PersistableRef Source;
    EquipmentDerivation Derivation = EquipmentDerivation::Pending;
};

struct SENCHA_COMPONENT("sencha.equipment_state") EquipmentState
{
    EquipmentLayoutId Layout;
    std::uint8_t Count = 0;
    std::uint32_t NextKey = 1;
    EquipmentRecord Records[kEquipmentRecordCapacity];
};

static_assert(std::is_trivially_copyable_v<EquipmentState>, "equipment components live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <equipment/EquipmentComponents.sencha.h>
#  include <equipment/EquipmentStateWire.h>
#endif
