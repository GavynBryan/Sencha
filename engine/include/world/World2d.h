#pragma once

#include <core/batch/DataBatch.h>
#include <core/service/IService.h>
#include <world/transform/core/TransformHierarchyServices.h>
#include <world/transform/core/TransformServiceTags.h>
#include <world/transform/core/TransformStore2D.h>
#include <world/transform/hierarchy/TransformPropagationOrderService.h>
#include <math/geometry/2d/Transform2d.h>

//=============================================================================
// World2d
//
// Gameplay-facing service bundle for 2D world state.
//=============================================================================
class World2d : public IService
{
	using TransformPropagationOrder =
		TransformPropagationOrderService<TransformServiceTags::Transform2DTag>;

private:
	DataBatch<Transform2f> LocalTransforms;
	DataBatch<Transform2f> WorldTransforms;
	TransformStore2D TransformsStorage;
	TransformHierarchy2DService TransformHierarchyStorage;
	TransformPropagationOrder PropagationOrder;

public:
	World2d()
		: TransformsStorage(LocalTransforms, WorldTransforms)
		, Transforms(TransformsStorage)
		, TransformHierarchy(TransformHierarchyStorage)
	{
	}

	DataBatch<Transform2f>& GetLocalTransformsForSystems() { return LocalTransforms; }
	DataBatch<Transform2f>& GetWorldTransformsForSystems() { return WorldTransforms; }
	TransformPropagationOrder& GetTransformPropagationOrderForSystems()
	{
		return PropagationOrder;
	}

	TransformStore2D& Transforms;
	TransformHierarchy2DService& TransformHierarchy;
};
