#pragma once

#include <ecs/EntityId.h>

#include <vector>

class EngineSchedule;
class World;
struct FixedLogicContext;

// A World resource. A stack merged away has no spender.
struct DepletedItems
{
    struct Entry
    {
        EntityId Item;
        EntityId Spender;
    };
    std::vector<Entry> Entries;
};

void NoteDepleted(World& world, EntityId entry, EntityId spender);

// docs/gameplay/inventory.md, "Spending, depletion and the inventory pass".
void RunInventoryPass(World& world);

class InventorySystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};

void RegisterInventorySystems(EngineSchedule& schedule);
