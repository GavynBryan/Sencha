#include <world/World2DSetup.h>

#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <math/geometry/2d/Transform2d.h>
#include <world/World2d.h>
#include <transform/TransformPropagationSystem.h>

namespace World2DSetup {

	namespace {
		World2d& GetOrAddWorld2d(ServiceHost& serviceHost,
		                         const PhysicsConfig2D& physicsConfig)
		{
			if (auto* world = serviceHost.TryGet<World2d>())
				return *world;

			return serviceHost.AddService<World2d>(physicsConfig);
		}
	}

	void Setup2D(ServiceHost& serviceHost, SystemHost& systemHost,
	             const PhysicsConfig2D& physicsConfig)
	{
		World2d& world = GetOrAddWorld2d(serviceHost, physicsConfig);

		// Fixed lane: TransformPropagationSystem has no dependencies.
		// RigidBodySyncSystem2D and RigidBodyResolutionSystem2D declare their
		// ordering in PhysicsSetup2D::Setup(), after they are registered there.
		systemHost.Register<TransformPropagationSystem<Transform2f>>(
			world.Transforms,
			world.Hierarchy,
			world.PropagationOrder);
	}
}
