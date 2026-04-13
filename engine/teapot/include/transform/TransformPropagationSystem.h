#pragma once

#include <batch/DataBatch.h>
#include <service/ServiceProvider.h>
#include <system/ISystem.h>
#include <transform/TransformHierarchyService.h>
#include <transform/TransformServiceTags.h>
#include <cstdint>

//=============================================================================
// TransformPropagationSystem
//
// Derives world transforms from local transforms by walking a transform-domain
// hierarchy. Operates on the TRANSFORM graph, not on any specific object model.
//
// Root entries: world = local.
// Parented entries: world = parent_world * local.
//
// TTransform must provide:
//   - static TTransform Identity()
//   - TTransform operator*(const TTransform&) const
//
// TDomainTag selects the hierarchy service. TLocalTag and TWorldTag select the
// local/world DataBatch roles inside ServiceHost.
//=============================================================================
template <
	typename TTransform,
	typename TDomainTag,
	typename TLocalTag = TransformServiceTags::LocalTransformTag,
	typename TWorldTag = TransformServiceTags::WorldTransformTag>
class TransformPropagationSystem : public ISystem
{
public:
	using TransformType = TTransform;
	using DomainTag = TDomainTag;
	using LocalTag = TLocalTag;
	using WorldTag = TWorldTag;
	using HierarchyType = TransformHierarchyService<TDomainTag>;

	explicit TransformPropagationSystem(const ServiceProvider& services)
		: Locals(services.GetTagged<DataBatch<TTransform>, TLocalTag>())
		, Worlds(services.GetTagged<DataBatch<TTransform>, TWorldTag>())
		, Hierarchy(services.Get<HierarchyType>())
	{
	}

	// Propagate can also be called manually (e.g., after spatial changes
	// outside the normal update loop).
	void Propagate()
	{
		auto roots = Hierarchy.GetRoots();
		for (DataBatchKey root : roots)
		{
			PropagateRecursive(root, TTransform::Identity());
		}
	}

private:
	void Update() override
	{
		Propagate();
	}

	void PropagateRecursive(DataBatchKey key, const TTransform& parentWorld)
	{
		const TTransform* local = Locals.TryGet(key);
		if (!local) return;

		TTransform world = parentWorld * (*local);

		TTransform* worldSlot = Worlds.TryGet(key);
		if (worldSlot)
		{
			*worldSlot = world;
		}

		const auto& children = Hierarchy.GetChildren(key);
		for (uint32_t childKey : children)
		{
			PropagateRecursive(DataBatchKey{ childKey }, world);
		}
	}

	DataBatch<TTransform>& Locals;
	DataBatch<TTransform>& Worlds;
	HierarchyType& Hierarchy;
};
