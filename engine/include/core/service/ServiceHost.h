#pragma once

#include <core/service/IService.h>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <memory>
#include <utility>

//=============================================================================
// ServiceHost
//
// Owns the engine's installed services and resolves them by concrete type.
// A service is any IService-derived capability whose construction sits in a
// backend dependency chain or otherwise needs ordered teardown (see IService).
//
// The host owns services in a single vector and destroys them in reverse
// insertion order (LIFO), so a service added later -- which may depend on one
// added earlier -- is destroyed first. That LIFO guarantee is the host's
// reason to exist; the Vulkan chain relies on it.
//
// The host is a wiring-time tool. Bootstrap code adds services and resolves
// them by type while assembling the engine; steady-state code holds the
// references it was handed and does not reach back through the host. Services
// take their own dependencies as explicit constructor parameters and never
// resolve siblings through the host.
//=============================================================================
class ServiceHost
{
public:
	ServiceHost* operator&() = delete;
	const ServiceHost* operator&() const = delete;

	ServiceHost() = default;
	~ServiceHost()
	{
		Clear();
	}

	ServiceHost(const ServiceHost&) = delete;
	ServiceHost& operator=(const ServiceHost&) = delete;
	ServiceHost(ServiceHost&&) = delete;
	ServiceHost& operator=(ServiceHost&&) = delete;

	template <typename T, typename... Args>
	T& AddService(Args&&... args);

	template <typename T>
	T& Get() const;

	template <typename T>
	T* TryGet() const;

	template <typename T>
	bool Has() const;

	void Clear();

private:
	std::vector<std::unique_ptr<IService>> Services;
	std::unordered_map<std::type_index, IService*> Registry;
};

template <typename T, typename... Args>
T& ServiceHost::AddService(Args&&... args)
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");

	auto service = std::make_unique<T>(std::forward<Args>(args)...);
	auto* rawService = service.get();

	Services.emplace_back(std::move(service));
	Registry[std::type_index(typeid(T))] = rawService;

	return *rawService;
}

template <typename T>
T& ServiceHost::Get() const
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	if (iter == Registry.end()) {
		throw std::runtime_error("Service not registered: " + std::string(typeid(T).name()));
	}
	return *static_cast<T*>(iter->second);
}

template <typename T>
T* ServiceHost::TryGet() const
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	if (iter == Registry.end()) {
		return nullptr;
	}
	return static_cast<T*>(iter->second);
}

template <typename T>
bool ServiceHost::Has() const
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	return Registry.find(std::type_index(typeid(T))) != Registry.end();
}

inline void ServiceHost::Clear()
{
	// Drop the registry first: it holds only raw pointers, so clearing it
	// before the owners avoids a dangling lookup if a destructor touches
	// sibling state during teardown. Then destroy owners in reverse insertion
	// order (LIFO).
	Registry.clear();
	while (!Services.empty())
	{
		Services.pop_back();
	}
}
