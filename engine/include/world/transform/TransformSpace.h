#pragma once

#include <core/batch/DataBatch.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformView.h>

//=============================================================================
// TransformSpace<TTransform>
//
// Self-contained transform space: a pair of local/world batches, a store that
// owns their paired lifetime, a hierarchy service for parenting, and a
// propagation-order cache. A domain is everything the TransformPropagationSystem
// needs to operate; it has no opinion on what lives in it.
//
// Any subsystem that wants its own isolated coordinate space creates a
// TransformSpace: the game simulation (via World), UI, the editor gizmo
// layer, a minimap, a particle sub-scene. They are peers, not a hierarchy of
// "the world and the rest" — each domain is complete on its own.
//
// Members are public because a space is a bundle, not an abstraction. Systems
// and engine wiring read the batches and services directly; gameplay code uses
// `Transforms` (the view) and `Hierarchy` for keyed access.
//=============================================================================
template <typename TTransform>
class TransformSpace
{
public:
	TransformSpace()
		: Transforms(LocalTransforms, WorldTransforms)
	{
	}

	TransformSpace(const TransformSpace&) = delete;
	TransformSpace& operator=(const TransformSpace&) = delete;
	TransformSpace(TransformSpace&&) = delete;
	TransformSpace& operator=(TransformSpace&&) = delete;

	DataBatch<TTransform> LocalTransforms;
	DataBatch<TTransform> WorldTransforms;
	TransformView<TTransform> Transforms;
	TransformHierarchyService Hierarchy;
	TransformPropagationOrderService PropagationOrder;
};

using TransformSpace2d = TransformSpace<Transform2f>;
using TransformSpace3d = TransformSpace<Transform3f>;
