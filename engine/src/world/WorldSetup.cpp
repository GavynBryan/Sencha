#include <world/WorldSetup.h>

#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/World.h>
#include <world/transform/TransformPropagationSystem.h>

namespace WorldSetup {

	namespace {
		World2d& GetOrAddWorld2d(ServiceHost& serviceHost,
		                         const PhysicsConfig2D& physicsConfig)
		{
			if (auto* world = serviceHost.TryGet<World2d>())
				return *world;

			return serviceHost.AddService<World2d>(physicsConfig);
		}

		World3d& GetOrAddWorld3d(ServiceHost& serviceHost)
		{
			if (auto* world = serviceHost.TryGet<World3d>())
				return *world;

			return serviceHost.AddService<World3d>();
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
			world.Domain.Transforms,
			world.Domain.Hierarchy,
			world.Domain.PropagationOrder);
	}

	void Setup3D(ServiceHost& serviceHost, SystemHost& systemHost)
	{
		World3d& world = GetOrAddWorld3d(serviceHost);

		systemHost.Register<TransformPropagationSystem<Transform3f>>(
			world.Domain.Transforms,
			world.Domain.Hierarchy,
			world.Domain.PropagationOrder);
	}
}
