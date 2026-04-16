#pragma once

#include <core/service/ServiceHost.h>
#include <core/logging/LoggingProvider.h>
#include <cassert>
#include <memory>

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
//       systems.Register<RenderSystem>(provider);
//       systems.Register<PhysicsSystem>(provider);
//   }
//   // provider is gone — systems hold only their cached service references
//=============================================================================
class ServiceProvider
{
public:
	explicit ServiceProvider(ServiceHost& host) : Host(std::addressof(host)) {}

	template<typename T>
	T& Get() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->Get<T>();
	}

	template<typename T, typename TTag>
	T& GetTagged() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->GetTagged<T, TTag>();
	}

	template<typename T>
	T* TryGet() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->TryGet<T>();
	}

	template<typename T, typename TTag>
	T* TryGetTagged() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->TryGetTagged<T, TTag>();
	}

	template<typename T>
	bool Has() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->Has<T>();
	}

	template<typename T, typename TTag>
	bool HasTagged() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->HasTagged<T, TTag>();
	}

	template <typename T>
	std::vector<T*> GetAll() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->GetAll<T>();
	}

	template <typename T, typename TTag>
	std::vector<T*> GetAllTagged() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->GetAllTagged<T, TTag>();
	}

	template <typename T>
	Logger& GetLogger() const
	{
		assert(Host && "ServiceProvider used after construction phase");
		return Host->GetLoggingProvider().GetLogger<T>();
	}

	// Manually invalidate â€” for use when scoped lifetime isn't sufficient
	void Invalidate() { Host = nullptr; }

	ServiceProvider(const ServiceProvider&) = delete;
	ServiceProvider& operator=(const ServiceProvider&) = delete;

private:
	ServiceHost* Host;
};
