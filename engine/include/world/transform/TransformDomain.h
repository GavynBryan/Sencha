#pragma once

#include <core/batch/DataBatch.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>

//=============================================================================
// TransformDomain<TTransform>
//
// Self-contained transform space: a pair of local/world batches, a store that
// owns their paired lifetime, a hierarchy service for parenting, and a
// propagation-order cache. A domain is everything the TransformPropagationSystem
// needs to operate; it has no opinion on what lives in it.
//
// Any subsystem that wants its own isolated coordinate space creates a
// TransformDomain: the game simulation (via World), UI, the editor gizmo
// layer, a minimap, a particle sub-scene. They are peers, not a hierarchy of
// "the world and the rest" — each domain is complete on its own.
//
// Members are public because a domain is a bundle, not an abstraction. Systems
// and engine wiring read the batches and services directly; gameplay code uses
// `Transforms` (the store) and `Hierarchy` for keyed access.
//=============================================================================
template <typename TTransform>
class TransformDomain
{
public:
	TransformDomain()
		: Transforms(LocalTransforms, WorldTransforms)
	{
	}

	TransformDomain(const TransformDomain&) = delete;
	TransformDomain& operator=(const TransformDomain&) = delete;
	TransformDomain(TransformDomain&&) = delete;
	TransformDomain& operator=(TransformDomain&&) = delete;

	DataBatch<TTransform> LocalTransforms;
	DataBatch<TTransform> WorldTransforms;
	TransformStore<TTransform> Transforms;
	TransformHierarchyService Hierarchy;
	TransformPropagationOrderService PropagationOrder;
};

using TransformDomain2d = TransformDomain<Transform2f>;
using TransformDomain3d = TransformDomain<Transform3f>;
