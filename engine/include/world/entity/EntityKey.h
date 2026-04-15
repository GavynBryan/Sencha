#pragma once

#include <core/batch/DataBatchKey.h>
#include <cstddef>
#include <functional>

//=============================================================================
// EntityKey
//
// Stable handle identifying an entity inside an EntityRegistry. Wraps a
// DataBatchKey so it carries generation information — stale keys stop
// resolving after the entity is destroyed and its slot is reused.
//
// Default-constructed EntityKey is null: operator bool() returns false.
//=============================================================================
struct EntityKey
{
	DataBatchKey Value;

	constexpr EntityKey() = default;
	constexpr explicit EntityKey(DataBatchKey key) : Value(key) {}

	explicit operator bool() const { return Value.Value != 0; }
	bool operator==(const EntityKey&) const = default;
};

template<> struct std::hash<EntityKey>
{
	std::size_t operator()(const EntityKey& k) const noexcept
	{
		return std::hash<uint32_t>{}(k.Value.Value);
	}
};
