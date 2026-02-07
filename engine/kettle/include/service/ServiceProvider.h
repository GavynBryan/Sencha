#pragma once

#include <service/ServiceHost.h>
#include <cassert>

//=============================================================================
// ServiceProvider
//
// A scoped, read-only view into ServiceHost that systems and components
// receive as a constructor parameter to resolve their dependencies.
//
// Designed to be short-lived: create one, pass it to constructors, then
// let it go out of scope or call Invalidate(). This removes any incentive
// for systems to cache a reference to the service host itself.
//
// Usage:
//   ServiceHost services;
//   // ... add services ...
//   {
//       ServiceProvider provider(services);
//       systems.AddSystem<RenderSystem>(0, provider);
//       systems.AddSystem<PhysicsSystem>(1, provider);
//   }
//   // provider is gone — systems hold only their cached service references
//=============================================================================
class ServiceProvider
{
public:
	explicit ServiceProvider(ServiceHost& host) : Host(&host) {}

	template<typename T>
	T& Get() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->Get<T>();
	}

	template<typename T>
	T* TryGet() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->TryGet<T>();
	}

    template <typename T>
    std::vector<T*> GetAll() const
    {
        assert(Host && "ServiceProvider used after construction phase");
        return Host->GetAll<T>();
    }

	// Manually invalidate — for use when scoped lifetime isn't sufficient
	void Invalidate() { Host = nullptr; }

	ServiceProvider(const ServiceProvider&) = delete;
	ServiceProvider& operator=(const ServiceProvider&) = delete;

private:
	ServiceHost* Host;
};
