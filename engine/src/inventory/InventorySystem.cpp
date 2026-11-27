#include <inventory/InventorySystem.h>

#include <abilities/AbilityActivations.h>
#include <abilities/AbilitySystem.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <ecs/World.h>
#include <inventory/InventoryComponents.h>
#include <world/SimulationAuthority.h>

#include <algorithm>

void NoteDepleted(World& world, EntityId entry, EntityId spender)
{
    DepletedItems& depleted =
        world.HasResource<DepletedItems>() ? world.GetResource<DepletedItems>() : world.AddResource<DepletedItems>();
    const auto found = std::ranges::find(depleted.Entries, entry, &DepletedItems::Entry::Item);
    if (found == depleted.Entries.end())
        depleted.Entries.push_back({ .Item = entry, .Spender = spender });
    else if (spender.IsValid())
        found->Spender = spender;
}

void RunInventoryPass(World& world)
{
    DepletedItems* depleted = world.TryGetResource<DepletedItems>();
    if (depleted == nullptr || !IsSimulationAuthority(world) || !world.IsRegistered<ItemEntry>())
        return;
    std::erase_if(depleted->Entries, [&](const DepletedItems::Entry& entry) {
        const ItemEntry* item = world.IsAlive(entry.Item) ? std::as_const(world).TryGet<ItemEntry>(entry.Item) : nullptr;
        if (item == nullptr || item->Count > 0)
            return true;
        if (entry.Spender.IsValid() && HasLiveActivationFrom(world, entry.Spender, entry.Item))
            return false;
        world.DestroyEntity(entry.Item);
        return true;
    });
}

void InventorySystem::FixedLogic(FixedLogicContext& ctx)
{
    RunInventoryPass(ctx.Entities);
}

void RegisterInventorySystems(EngineSchedule& schedule)
{
    schedule.Register<InventorySystem>();
    if (schedule.Has<AbilitySystem>())
        schedule.After<InventorySystem, AbilitySystem>();
}
