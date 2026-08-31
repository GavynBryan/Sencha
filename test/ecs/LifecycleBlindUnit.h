#pragma once

// Support for the trait-blind translation unit test: the in-process analog of a
// game module that names a component without seeing the header its lifecycle
// hooks were declared in.
//
// This header deliberately declares no ComponentTraits specialization. The unit
// that implements these functions includes only this header, so its structural
// calls compile with the primary (empty) ComponentTraits in scope. Hooks must
// still fire, because the World dispatches what registration captured rather
// than what the calling unit can see.

#include <ecs/EntityId.h>
#include <ecs/World.h>

#include <vector>

struct BlindHooked
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(BlindHooked, "test.lifecycle.blind_hooked");

// All defined in LifecycleBlindUnit.cpp, which cannot see the traits.
void AddBlindHookedFromBlindUnit(World& world, EntityId entity, int value);
void RemoveBlindHookedFromBlindUnit(World& world, EntityId entity);
void AddBlindHookedViaCommandBufferFromBlindUnit(World& world, EntityId entity, int value);
void RemoveBlindHookedViaCommandBufferFromBlindUnit(World& world, EntityId entity);

// Runs of identical commands, which are what Flush may coalesce. Values are the
// entities' positions in the vector.
void AddBlindHookedRunViaCommandBufferFromBlindUnit(
    World& world, const std::vector<EntityId>& entities);
void RemoveBlindHookedRunViaCommandBufferFromBlindUnit(
    World& world, const std::vector<EntityId>& entities);
void InitializeBlindHookedFromBlindUnit(World& world, EntityId entity, int value);
