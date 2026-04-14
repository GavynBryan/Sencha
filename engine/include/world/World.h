#pragma once

#include <core/service/IService.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/transform/TransformDomain.h>

//=============================================================================
// World<TTransform>
//
// Gameplay-facing service bundle for transform-dimensioned game-world state.
// World owns a TransformDomain (the self-contained transform space used by
// the game simulation) and registers with ServiceHost so gameplay code can
// resolve it and reach transform state through `Transforms` and
// `TransformHierarchy`.
//
// World is NOT the only TransformDomain in the engine. UI, editor gizmos, or
// any other subsystem that wants an isolated coordinate space creates its own
// TransformDomain directly — no World involvement, no service registration,
// no hierarchy conflicts with gameplay. World is simply "the domain that
// belongs to the game simulation."
//
// Each specialization is a distinct service type in ServiceHost (tagged by
// typeid), so World2d and World3d are resolved independently.
//=============================================================================
template <typename TTransform>
class World : public IService
{
public:
	World()
		: Transforms(Domain.Transforms)
		, TransformHierarchy(Domain.Hierarchy)
	{
	}

	// -- The transform space owned by this world --------------------------

	TransformDomain<TTransform> Domain;

	// -- Gameplay-facing shortcuts (forward into Domain) ------------------

	TransformStore<TTransform>& Transforms;
	TransformHierarchyService& TransformHierarchy;
};

// -- Common aliases --------------------------------------------------------

using World2d = World<Transform2f>;
using World3d = World<Transform3f>;
