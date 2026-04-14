#pragma once

#include <core/batch/DataBatchKey.h>
#include <core/raii/DataBatchHandle.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/transform/TransformDomain.h>
#include <world/transform/TransformHierarchyRegistration.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformStore.h>

//=============================================================================
// TransformNode<TTransform>
//
// Rule-of-zero composition of a transform slot + hierarchy registration.
// This is the reusable primitive for "an object that participates in a
// TransformDomain and can be parented to other participants in the same
// domain." It is not specific to the game scene — UI, editor gizmos, or any
// other subsystem that owns a TransformDomain can compose TransformNode into
// its own types to get parenting and lifetime for free.
//
// Complex types compose a TransformNode alongside their own state — they do
// not inherit from it. No virtuals, no vtable, no Update().
//
// Ownership order matters: the hierarchy registration is destroyed before the
// transform-batch handle so that the hierarchy service never observes a key
// whose transform slot has already been freed. Member declaration order below
// reflects that: Handle first, Registration second → Registration destroyed
// first on teardown.
//=============================================================================
template <typename TTransform>
class TransformNode
{
public:
	using TransformType = TTransform;

	TransformNode(TransformDomain<TTransform>& domain, const TTransform& initialLocal)
		: Handle(domain.Transforms.Emplace(initialLocal))
		, Registration(domain.Hierarchy, Handle.GetToken())
	{
	}

	TransformNode(TransformNode&&) noexcept = default;
	TransformNode& operator=(TransformNode&&) noexcept = default;
	TransformNode(const TransformNode&) = delete;
	TransformNode& operator=(const TransformNode&) = delete;

	DataBatchKey TransformKey() const { return Handle.GetToken(); }

	// -- Parenting (routes through the hierarchy service) -------------------

	void SetParent(const TransformNode& parent)
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
	DataBatchHandle<TTransform> Handle;
	TransformHierarchyRegistration Registration;
};

// -- Common aliases --------------------------------------------------------

using TransformNode2d = TransformNode<Transform2f>;
using TransformNode3d = TransformNode<Transform3f>;
