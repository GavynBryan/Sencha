#pragma once

#include <entity/EntityHandle.h>

//=============================================================================
// IsEntity concept
//
// Satisfied by any type that exposes a Handle() const -> EntityHandle method.
// Used to constrain EntityBatch<T> so only types with a valid entity identity
// can be stored as entities.
//
// The minimal conforming struct:
//
//   struct Goblin
//   {
//       EntityHandle Entity;
//       EntityHandle Handle() const { return Entity; }
//   };
//=============================================================================
template <typename T>
concept IsEntity = requires(const T& t)
{
	{ t.Handle() } -> std::same_as<EntityHandle>;
};
