#pragma once

#include <core/batch/DataBatch.h>
#include <core/raii/DataBatchHandle.h>
#include <core/raii/ILifetimeOwner.h>
#include <core/service/IService.h>
#include <world/transform/core/TransformServiceTags.h>
#include <math/geometry/3d/Transform3d.h>
#include <cstdint>
#include <cstring>
#include <span>

//=============================================================================
// TransformStore3D
//
// Gameplay-facing allocation service for paired local/world 3D transforms.
// The returned handle is owned by the store, so destruction removes both slots.
//=============================================================================
class TransformStore3D : public IService, public ILifetimeOwner
{
public:
	TransformStore3D(
		DataBatch<Transform3f>& locals,
		DataBatch<Transform3f>& worlds)
		: Locals(locals)
		, Worlds(worlds)
	{
	}

	DataBatchHandle<Transform3f> Emplace(const Transform3f& local)
	{
		const DataBatchKey key = Locals.EmplaceUnowned(local);
		try
		{
			Worlds.EmplaceAtKey(key, Transform3f::Identity());
		}
		catch (...)
		{
			Locals.RemoveKey(key);
			throw;
		}

		return DataBatchHandle<Transform3f>(this, key);
	}

	const Transform3f* TryGetLocal(DataBatchKey key) const
	{
		return Locals.TryGet(key);
	}

	const Transform3f* TryGetWorld(DataBatchKey key) const
	{
		return Worlds.TryGet(key);
	}

	Transform3f* TryGetLocalMutable(DataBatchKey key)
	{
		return Locals.TryGet(key);
	}

	std::span<const Transform3f> GetWorldsSpan() const
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
	DataBatch<Transform3f>& Locals;
	DataBatch<Transform3f>& Worlds;
};
