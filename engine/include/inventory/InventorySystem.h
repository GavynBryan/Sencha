#pragma once

#include <ecs/EntityId.h>

#include <vector>

class EngineSchedule;
class World;
struct FixedLogicContext;

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

void RunInventoryPass(World& world);

class InventorySystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};

void RegisterInventorySystems(EngineSchedule& schedule);
