#pragma once

#include <core/batch/DataBatchKey.h>
#include <core/raii/DataBatchHandle.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/scene/SceneNodeCore.h>
#include <world/transform/TransformHierarchyRegistration.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformStore.h>
#include <world/World.h>

//=============================================================================
// SceneNodeWorldTraits<TTransform>
//
// Picks the concrete World type that owns the TransformStore<TTransform> and
// the TransformHierarchyService a SceneNode<TTransform> participates in.
// Specialized for Transform2f (World2d) and Transform3f (World3d); adding a
// new dimension means adding a new specialization plus a new World class.
//=============================================================================
template <typename TTransform>
struct SceneNodeWorldTraits;

template <>
struct SceneNodeWorldTraits<Transform2f>
{
	using WorldType = World2d;
};

template <>
struct SceneNodeWorldTraits<Transform3f>
{
	using WorldType = World3d;
};

//=============================================================================
// SceneNode<TTransform>
//
// Thin composition of a transform handle + hierarchy registration + identity.
// Lifetime is rule-of-zero: the two RAII members clean themselves up in the
// right order (registration first, then transform slot). Complex gameplay
// objects compose a SceneNode alongside their own state — they do not inherit
// from it. No virtuals, no vtable, no Update().
//=============================================================================
template <typename TTransform>
class SceneNode
{
public:
	using TransformType = TTransform;
	using WorldType = typename SceneNodeWorldTraits<TTransform>::WorldType;

	SceneNode(WorldType& world, const TTransform& initialLocal)
		: Handle(world.Transforms.Emplace(initialLocal))
		, Registration(world.TransformHierarchy, Handle.GetToken())
	{
	}

	SceneNode(SceneNode&&) noexcept = default;
	SceneNode& operator=(SceneNode&&) noexcept = default;
	SceneNode(const SceneNode&) = delete;
	SceneNode& operator=(const SceneNode&) = delete;

	// -- Identity -----------------------------------------------------------

	SceneNodeCore& Core() { return CoreData; }
	const SceneNodeCore& Core() const { return CoreData; }

	DataBatchKey TransformKey() const { return Handle.GetToken(); }

	// -- Parenting (routes through the hierarchy service) ------------------

	void SetParent(const SceneNode& parent)
	{
		Registration.GetService()->SetParent(TransformKey(), parent.TransformKey());
	}

	void ClearParent()
	{
		Registration.GetService()->ClearParent(TransformKey());
	}

	bool HasParent() const
	{
		return Registration.GetService()->HasParent(TransformKey());
	}

	DataBatchKey GetParentKey() const
	{
		return Registration.GetService()->GetParent(TransformKey());
	}

private:
	SceneNodeCore CoreData;
	DataBatchHandle<TTransform> Handle;
	TransformHierarchyRegistration Registration;
};

// -- Common aliases --------------------------------------------------------

using SceneNode2D = SceneNode<Transform2f>;
using SceneNode3D = SceneNode<Transform3f>;
