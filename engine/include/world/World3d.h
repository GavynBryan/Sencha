#pragma once

#include <core/batch/DataBatch.h>
#include <core/service/IService.h>
#include <world/transform/TransformStore.h>
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
// Gameplay-facing service bundle for 3D world state. Gameplay code resolves
// this from the ServiceHost and reaches transform state through `Transforms`
// and `TransformHierarchy`. Systems are invisible to gameplay; they are wired
// at engine init by WorldSetup::Setup3D, which is granted direct access to
// the raw batch storage through friendship.
//
// The `*ForEngineWiring()` accessors exist ONLY for engine-internal system
// wiring (WorldSetup, test harnesses that emulate it). Gameplay code MUST NOT
// call them — use `Transforms` and `TransformHierarchy` instead.
//=============================================================================
class World3d : public IService
{
	friend void WorldSetup::Setup3D(ServiceHost&, SystemHost&);

public:
	World3d()
		: TransformsStorage(LocalTransforms, WorldTransforms)
		, Transforms(TransformsStorage)
		, TransformHierarchy(TransformHierarchyStorage)
	{
	}

	// -- Gameplay-facing ----------------------------------------------------

	TransformStore3D& Transforms;
	TransformHierarchyService& TransformHierarchy;

	// -- Engine-internal wiring (systems, test harnesses) ------------------

	DataBatch<Transform3f>& GetLocalTransformsForEngineWiring() { return LocalTransforms; }
	DataBatch<Transform3f>& GetWorldTransformsForEngineWiring() { return WorldTransforms; }
	TransformPropagationOrderService& GetPropagationOrderForEngineWiring() { return PropagationOrder; }

private:
	DataBatch<Transform3f> LocalTransforms;
	DataBatch<Transform3f> WorldTransforms;
	TransformStore3D TransformsStorage;
	TransformHierarchyService TransformHierarchyStorage;
	TransformPropagationOrderService PropagationOrder;
};
