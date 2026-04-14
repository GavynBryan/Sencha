#pragma once

#include <core/batch/DataBatch.h>
#include <core/raii/DataBatchHandle.h>
#include <core/raii/ILifetimeOwner.h>
#include <core/service/IService.h>
#include <world/transform/core/TransformServiceTags.h>
#include <math/geometry/2d/Transform2d.h>
#include <cstdint>
#include <cstring>
#include <span>

//=============================================================================
// TransformStore2D
//
// Gameplay-facing allocation service for paired local/world 2D transforms.
// The returned handle is owned by the store, so destruction removes both slots.
//=============================================================================
class TransformStore2D : public IService, public ILifetimeOwner
{
public:
	TransformStore2D(
		DataBatch<Transform2f>& locals,
		DataBatch<Transform2f>& worlds)
		: Locals(locals)
		, Worlds(worlds)
	{
	}

	DataBatchHandle<Transform2f> Emplace(const Transform2f& local)
	{
		const DataBatchKey key = Locals.EmplaceUnowned(local);
		try
		{
			Worlds.EmplaceAtKey(key, Transform2f::Identity());
		}
		catch (...)
		{
			Locals.RemoveKey(key);
			throw;
		}

		return DataBatchHandle<Transform2f>(this, key);
	}

	const Transform2f* TryGetLocal(DataBatchKey key) const
	{
		return Locals.TryGet(key);
	}

	const Transform2f* TryGetWorld(DataBatchKey key) const
	{
		return Worlds.TryGet(key);
	}

	Transform2f* TryGetLocalMutable(DataBatchKey key)
	{
		return Locals.TryGet(key);
	}

	std::span<const Transform2f> GetWorldsSpan() const
	{
		return Worlds.GetItems();
	}

protected:
	void Attach(uint64_t /*token*/) override {}

	void Detach(uint64_t token) override
	{
		DataBatchKey key{};
		std::memcpy(&key, &token, sizeof(key));
		Worlds.RemoveKey(key);
		Locals.RemoveKey(key);
	}

private:
	DataBatch<Transform2f>& Locals;
	DataBatch<Transform2f>& Worlds;
};
