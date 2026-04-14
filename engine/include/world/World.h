#pragma once

#include <core/batch/DataBatch.h>
#include <core/service/IService.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>

class ServiceHost;
class SystemHost;

namespace WorldSetup {
	void Setup2D(ServiceHost&, SystemHost&);
	void Setup3D(ServiceHost&, SystemHost&);
}

//=============================================================================
// World<TTransform>
//
// Gameplay-facing service bundle for transform-dimensioned world state.
// Gameplay code resolves this from the ServiceHost and reaches transform
// state through `Transforms` and `TransformHierarchy`. Systems are invisible
// to gameplay; they are wired at engine init by WorldSetup::Setup2D /
// Setup3D, which are granted direct access to the raw batch storage through
// friendship.
//
// The `*ForEngineWiring()` accessors exist ONLY for engine-internal system
// wiring (WorldSetup, test harnesses that emulate it). Gameplay code MUST
// NOT call them — use `Transforms` and `TransformHierarchy` instead.
//
// Each specialization is a distinct service type in ServiceHost (tagged by
// typeid), so World2d and World3d are resolved independently.
//=============================================================================
template <typename TTransform>
class World : public IService
{
	friend void WorldSetup::Setup2D(ServiceHost&, SystemHost&);
	friend void WorldSetup::Setup3D(ServiceHost&, SystemHost&);

public:
	World()
		: TransformsStorage(LocalTransforms, WorldTransforms)
		, Transforms(TransformsStorage)
		, TransformHierarchy(TransformHierarchyStorage)
	{
	}

	// -- Gameplay-facing ----------------------------------------------------

	TransformStore<TTransform>& Transforms;
	TransformHierarchyService& TransformHierarchy;

	// -- Engine-internal wiring (systems, test harnesses) ------------------

	DataBatch<TTransform>& GetLocalTransformsForEngineWiring() { return LocalTransforms; }
	DataBatch<TTransform>& GetWorldTransformsForEngineWiring() { return WorldTransforms; }
	TransformPropagationOrderService& GetPropagationOrderForEngineWiring() { return PropagationOrder; }

private:
	DataBatch<TTransform> LocalTransforms;
	DataBatch<TTransform> WorldTransforms;
	TransformStore<TTransform> TransformsStorage;
	TransformHierarchyService TransformHierarchyStorage;
	TransformPropagationOrderService PropagationOrder;
};

// -- Common aliases --------------------------------------------------------

using World2d = World<Transform2f>;
using World3d = World<Transform3f>;
