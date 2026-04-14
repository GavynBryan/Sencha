#pragma once

#include <core/batch/DataBatch.h>
#include <core/service/IService.h>
#include <world/transform/TransformStore.h>
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
// Gameplay-facing service bundle for 2D world state. Gameplay code resolves
// this from the ServiceHost and reaches transform state through `Transforms`
// and `TransformHierarchy`. Systems are invisible to gameplay; they are wired
// at engine init by WorldSetup::Setup2D, which is granted direct access to
// the raw batch storage through friendship.
//
// The `*ForEngineWiring()` accessors exist ONLY for engine-internal system
// wiring (WorldSetup, test harnesses that emulate it). Gameplay code MUST NOT
// call them — use `Transforms` and `TransformHierarchy` instead.
//=============================================================================
class World2d : public IService
{
	friend void WorldSetup::Setup2D(ServiceHost&, SystemHost&);

public:
	World2d()
		: TransformsStorage(LocalTransforms, WorldTransforms)
		, Transforms(TransformsStorage)
		, TransformHierarchy(TransformHierarchyStorage)
	{
	}

	// -- Gameplay-facing ----------------------------------------------------

	TransformStore2D& Transforms;
	TransformHierarchyService& TransformHierarchy;

	// -- Engine-internal wiring (systems, test harnesses) ------------------
	//
	// These return the raw DataBatch storage and propagation cache used by
	// TransformPropagationSystem. Not a gameplay surface.

	DataBatch<Transform2f>& GetLocalTransformsForEngineWiring() { return LocalTransforms; }
	DataBatch<Transform2f>& GetWorldTransformsForEngineWiring() { return WorldTransforms; }
	TransformPropagationOrderService& GetPropagationOrderForEngineWiring() { return PropagationOrder; }

private:
	DataBatch<Transform2f> LocalTransforms;
	DataBatch<Transform2f> WorldTransforms;
	TransformStore2D TransformsStorage;
	TransformHierarchyService TransformHierarchyStorage;
	TransformPropagationOrderService PropagationOrder;
};
