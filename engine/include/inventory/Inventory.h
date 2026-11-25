#pragma once

#include <core/metadata/EnumSchema.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <inventory/ItemDefinition.h>
#include <math/geometry/3d/Transform3d.h>
#include <time/FrameClock.h>

#include <cstdint>
#include <vector>

class World;

inline constexpr std::uint32_t kItemContainerMostEntries = 0xFFFF;

enum class InventoryRefusal : std::uint8_t
{
    None,
    NotAuthority,
    Missing,
    NotAccepted,
    ContainerFull,
    TooDeep,
    NotPermitted,
    Withheld,
};

template <>
struct EnumSchema<InventoryRefusal>
{
    static constexpr std::array Values = {
        EnumValue{ InventoryRefusal::None, "none" },
        EnumValue{ InventoryRefusal::NotAuthority, "not_authority" },
        EnumValue{ InventoryRefusal::Missing, "missing" },
        EnumValue{ InventoryRefusal::NotAccepted, "not_accepted" },
        EnumValue{ InventoryRefusal::ContainerFull, "container_full" },
        EnumValue{ InventoryRefusal::TooDeep, "too_deep" },
        EnumValue{ InventoryRefusal::NotPermitted, "not_permitted" },
        EnumValue{ InventoryRefusal::Withheld, "withheld" },
    };
};

enum class ItemChangeClass : std::uint8_t
{
    Requested,
    Forced,
};

struct ItemMoveAdmission
{
    bool (*Admit)(const World& world, EntityId entry) = nullptr;
    void (*Moved)(World& world, EntityId entry, EntityId formerHolder) = nullptr;
};

// False, leaving the World's admission as it is, when another layer installed a different one.
[[nodiscard]] bool InstallItemMoveAdmission(World& world, const ItemMoveAdmission& admission);

struct InventoryResult
{
    InventoryRefusal Refusal = InventoryRefusal::None;
    EntityId Entry;
};

[[nodiscard]] const ItemDefinition* FindItemDefinition(const World& world, ItemId item);
[[nodiscard]] std::vector<EntityId> ItemsIn(const World& world, EntityId container);
[[nodiscard]] std::vector<EntityId> HeldEntries(const World& world, EntityId holder);
[[nodiscard]] EntityId ItemHolder(const World& world, EntityId item);
[[nodiscard]] bool MayUseItem(const World& world, EntityId actor, EntityId item);
[[nodiscard]] bool ItemIsWithin(const World& world, EntityId inner, EntityId outer);
[[nodiscard]] std::uint32_t ItemStackClamp(const ItemDefinition& item, std::uint32_t count);
[[nodiscard]] bool ItemStackHolds(const ItemDefinition& item, std::uint32_t count);
void SetItemCount(World& world, EntityId entry, std::uint32_t count, EntityId spender = {});
[[nodiscard]] bool ItemCarries(const World& world, const ItemDefinition& item, GameplayTagId tag);

// Owner thread, outside any query, in the tick `now`.
class Inventory
{
public:
    Inventory(World& world, const FixedSimTime& now)
        : Entities(&world)
        , Now(now)
    {
    }

    InventoryResult Give(EntityId container, ItemId definition, std::uint32_t count);
    InventoryResult Take(EntityId pickup, EntityId into);
    InventoryResult Transfer(EntityId entry, EntityId into, std::uint32_t count,
                             ItemChangeClass change = ItemChangeClass::Requested);
    InventoryResult Drop(EntityId entry, const Transform3f& at, StoragePartitionId partition,
                         ItemChangeClass change = ItemChangeClass::Requested);
    InventoryResult Use(EntityId user, EntityId entry);

private:
    World* Entities;
    FixedSimTime Now;
};
