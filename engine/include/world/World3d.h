#pragma once

#include <core/batch/DataBatch.h>
#include <core/service/IService.h>
#include <world/transform/TransformStore3D.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <math/geometry/3d/Transform3d.h>

class ServiceHost;
class SystemHost;

namespace WorldSetup {
	void Setup3D(ServiceHost&, SystemHost&);
}

//=============================================================================
// World3d
//
// Gameplay-facing service bundle for 3D world state.
//=============================================================================
class World3d : public IService
{
	friend void WorldSetup::Setup3D(ServiceHost&, SystemHost&);

private:
	DataBatch<Transform3f> LocalTransforms;
	DataBatch<Transform3f> WorldTransforms;
	TransformStore3D TransformsStorage;
	TransformHierarchyService TransformHierarchyStorage;
	TransformPropagationOrderService PropagationOrder;

public:
	DataBatch<Transform3f>& GetLocalTransformsForSystems() { return LocalTransforms; }
	DataBatch<Transform3f>& GetWorldTransformsForSystems() { return WorldTransforms; }
	TransformPropagationOrderService& GetTransformPropagationOrderForSystems()
	{
		return PropagationOrder;
	}

	World3d()
		: TransformsStorage(LocalTransforms, WorldTransforms)
		, Transforms(TransformsStorage)
		, TransformHierarchy(TransformHierarchyStorage)
	{
	}

	TransformStore3D& Transforms;
	TransformHierarchyService& TransformHierarchy;
};
