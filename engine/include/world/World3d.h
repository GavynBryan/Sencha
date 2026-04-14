#pragma once

#include <core/batch/DataBatch.h>
#include <core/service/IService.h>
#include <world/transform/core/TransformHierarchyServices.h>
#include <world/transform/core/TransformServiceTags.h>
#include <world/transform/core/TransformStore3D.h>
#include <world/transform/hierarchy/TransformPropagationOrderService.h>
#include <math/geometry/3d/Transform3d.h>

//=============================================================================
// World3d
//
// Gameplay-facing service bundle for 3D world state.
//=============================================================================
class World3d : public IService
{
	using TransformPropagationOrder =
		TransformPropagationOrderService<TransformServiceTags::Transform3DTag>;

private:
	DataBatch<Transform3f> LocalTransforms;
	DataBatch<Transform3f> WorldTransforms;
	TransformStore3D TransformsStorage;
	TransformHierarchy3DService TransformHierarchyStorage;
	TransformPropagationOrder PropagationOrder;

public:
	World3d()
		: TransformsStorage(LocalTransforms, WorldTransforms)
		, Transforms(TransformsStorage)
		, TransformHierarchy(TransformHierarchyStorage)
	{
	}

	DataBatch<Transform3f>& GetLocalTransformsForSystems() { return LocalTransforms; }
	DataBatch<Transform3f>& GetWorldTransformsForSystems() { return WorldTransforms; }
	TransformPropagationOrder& GetTransformPropagationOrderForSystems()
	{
		return PropagationOrder;
	}

	TransformStore3D& Transforms;
	TransformHierarchy3DService& TransformHierarchy;
};
