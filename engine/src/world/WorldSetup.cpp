#include <world/WorldSetup.h>

#include <core/service/ServiceHost.h>
#include <physics/2d/PhysicsDomain2D.h>
#include <core/system/SystemHost.h>
#include <core/system/SystemPhase.h>
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

		systemHost.AddSystem<TransformPropagationSystem<Transform2f>>(
			SystemPhase::PostUpdate,
			world.Domain.LocalTransforms,
			world.Domain.WorldTransforms,
			world.Domain.Hierarchy,
			world.Domain.PropagationOrder);
	}

	void Setup3D(ServiceHost& serviceHost, SystemHost& systemHost)
	{
		World3d& world = GetOrAddWorld3d(serviceHost);

		systemHost.AddSystem<TransformPropagationSystem<Transform3f>>(
			SystemPhase::PostUpdate,
			world.Domain.LocalTransforms,
			world.Domain.WorldTransforms,
			world.Domain.Hierarchy,
			world.Domain.PropagationOrder);
	}
}
