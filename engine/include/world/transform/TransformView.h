#pragma once

#include <core/batch/DataBatch.h>
#include <core/handle/DataBatchHandle.h>
#include <core/handle/ILifetimeOwner.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <cstdint>
#include <cstring>
#include <span>

//=============================================================================
// TransformView<TTransform>
//
// Gameplay-facing allocation service for paired local/world transforms.
// Emplace() puts an entry in both the local and world batches under a shared
// key, and the returned DataBatchHandle cleans up both slots on destruction.
//
// Access rules (see also World<dim>d.h):
//   - Keyed access (TryGet*) is for gameplay, scripts, UI, setup. One array
//     lookup per call; fine at gameplay frequencies.
//   - Bulk span access is for systems sweeping every transform. Propagation,
//     culling, and renderer submission should prefer this path.
//   - World transforms are read-only to the public API; only the propagation
//     system (which the World grants direct batch access) is allowed to write
//     them.
//=============================================================================
template <typename TTransform>
class TransformView : public ILifetimeOwner
{
public:
	using TransformType = TTransform;

	TransformView(
		DataBatch<TTransform>& locals,
		DataBatch<TTransform>& worlds)
		: Locals(locals)
		, Worlds(worlds)
	{
	}

	// -- Allocation ---------------------------------------------------------

	DataBatchHandle<TTransform> Emplace(const TTransform& local)
	{
		const DataBatchKey key = Locals.EmplaceUnowned(local);
		try
		{
			Worlds.EmplaceAtKey(key, TTransform::Identity());
		}
		catch (...)
		{
			Locals.RemoveKey(key);
			throw;
		}

		return DataBatchHandle<TTransform>(this, key);
	}

	// -- Keyed access (gameplay) -------------------------------------------

	const TTransform* TryGetLocal(DataBatchKey key) const
	{
		return Locals.TryGet(key);
	}

	const TTransform* TryGetWorld(DataBatchKey key) const
	{
		return Worlds.TryGet(key);
	}

	TTransform* TryGetLocalMutable(DataBatchKey key)
	{
		return Locals.TryGet(key);
	}

	// -- Bulk access (systems, renderers) ----------------------------------

	std::span<const TTransform> GetLocalsSpan() const { return Locals.GetItems(); }
	std::span<const TTransform> GetWorldsSpan() const { return Worlds.GetItems(); }

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
	DataBatch<TTransform>& Locals;
	DataBatch<TTransform>& Worlds;
};

// -- Common aliases --------------------------------------------------------

using TransformView2D = TransformView<Transform2f>;
using TransformView3D = TransformView<Transform3f>;
