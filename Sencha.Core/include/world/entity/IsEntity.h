#pragma once

#include <core/batch/DataBatchKey.h>

//=============================================================================
// IsEntity concept
//
// Satisfied by any type that exposes a TransformKey() const -> DataBatchKey
// method. Used to constrain EntityBatch<T> so only types with a valid
// transform key can be stored as entities.
//
// The minimal conforming struct:
//
//   struct Goblin
//   {
//       TransformNode2d Node;
//       DataBatchKey TransformKey() const { return Node.TransformKey(); }
//   };
//=============================================================================
template <typename T>
concept IsEntity = requires(const T& t)
{
	{ t.TransformKey() } -> std::same_as<DataBatchKey>;
};
