#pragma once

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <inventory/ItemDefinition.h>
#include <math/geometry/3d/Transform3d.h>
#include <time/FrameClock.h>

#include <cstdint>
#include <vector>

class World;

// What a capacity of zero means.
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

enum class ItemChangeClass : std::uint8_t
{
    Requested,
    Forced,
};

// A World resource: docs/gameplay/inventory.md, "Operations".
struct ItemMoveAdmission
{
    bool (*Admit)(const World& world, EntityId entry) = nullptr;
    void (*Moved)(World& world, EntityId entry, EntityId formerHolder) = nullptr;
};

struct InventoryResult
{
    InventoryRefusal Refusal = InventoryRefusal::None;
    // For a drop, the pickup.
    EntityId Entry;
};

[[nodiscard]] const ItemDefinition* FindItemDefinition(const World& world, ItemId item);
// Depleted entries included.
[[nodiscard]] std::vector<EntityId> ItemsIn(const World& world, EntityId container);
// Depth-first, so a bag's contents follow the bag.
[[nodiscard]] std::vector<EntityId> HeldEntries(const World& world, EntityId holder);
// An entity that is not an entry is its own holder.
[[nodiscard]] EntityId ItemHolder(const World& world, EntityId item);
// docs/gameplay/inventory.md, "Who may use an entry".
[[nodiscard]] bool MayUseItem(const World& world, EntityId actor, EntityId item);
[[nodiscard]] bool ItemIsWithin(const World& world, EntityId inner, EntityId outer);
[[nodiscard]] bool ItemStackHolds(const ItemDefinition& item, std::uint32_t count);
// Authority only. The one write of an entry's count; reaching zero leaves it for the inventory pass.
void SetItemCount(World& world, EntityId entry, std::uint32_t count, EntityId spender = {});
// Every item carries the invalid tag.
[[nodiscard]] bool ItemCarries(const World& world, const ItemDefinition& item, GameplayTagId tag);

// docs/gameplay/inventory.md, "Operations". Owner thread, outside any query, in the tick `now`.
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
    // A count of 0 moves all of it.
    InventoryResult Transfer(EntityId entry, EntityId into, std::uint32_t count,
                             ItemChangeClass change = ItemChangeClass::Requested);
    InventoryResult Drop(EntityId entry, const Transform3f& at, StoragePartitionId partition,
                         ItemChangeClass change = ItemChangeClass::Requested);
    InventoryResult Use(EntityId user, EntityId entry);

private:
    World* Entities;
    FixedSimTime Now;
};
