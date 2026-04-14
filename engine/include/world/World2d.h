#pragma once

#include <core/batch/DataBatch.h>
#include <core/service/IService.h>
#include <world/transform/TransformStore2D.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <math/geometry/2d/Transform2d.h>

class ServiceHost;
class SystemHost;

namespace WorldSetup {
	void Setup2D(ServiceHost&, SystemHost&);
}

//=============================================================================
// World2d
//
// Gameplay-facing service bundle for 2D world state.
//=============================================================================
class World2d : public IService
{
	friend void WorldSetup::Setup2D(ServiceHost&, SystemHost&);

private:
	DataBatch<Transform2f> LocalTransforms;
	DataBatch<Transform2f> WorldTransforms;
	TransformStore2D TransformsStorage;
	TransformHierarchyService TransformHierarchyStorage;
	TransformPropagationOrderService PropagationOrder;

public:
	DataBatch<Transform2f>& GetLocalTransformsForSystems() { return LocalTransforms; }
	DataBatch<Transform2f>& GetWorldTransformsForSystems() { return WorldTransforms; }
	TransformPropagationOrderService& GetTransformPropagationOrderForSystems()
	{
		return PropagationOrder;
	}

	World2d()
		: TransformsStorage(LocalTransforms, WorldTransforms)
		, Transforms(TransformsStorage)
		, TransformHierarchy(TransformHierarchyStorage)
	{
	}

	TransformStore2D& Transforms;
	TransformHierarchyService& TransformHierarchy;
};
