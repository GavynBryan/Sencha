#pragma once

#include <ecs/EntityId.h>

class World;

//=============================================================================
// DerivedTransform
//
// WorldTransform is derived, never authored: data declares a LocalTransform and
// the entity built from it gets a matching world transform to propagate from.
//
// Every path that builds an entity from data owes this, and there is more than
// one of them -- zone import reads a cooked package, replication reads a
// snapshot -- so the obligation lives here rather than in each of them. An
// entity that ends up with a local transform and no world one is invisible to
// everything downstream: extraction reads WorldTransform, and so does the pose
// history that feeds interpolated presentation. Nothing errors; the entity
// simply never appears.
//
// Uses the in-place write when the row already carries the column (the caller
// built it at its final signature) and falls back to adding it when it does not.
// Inert in a world that does not register the transform pair at all, which is
// how a headless or tool world with a different vocabulary stays valid.
//=============================================================================
void SeedDerivedWorldTransform(World& world, EntityId entity);
