#pragma once

#include <service/BatchArray.h>
#include <service/IService.h>
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <memory>
#include <utility>

class ServiceHost
{
public:
	template <typename T, typename TInterface = T, typename... Args>
	T& AddService(Args&&... args);

	template <typename T>
	T& Get() const;

	template <typename T>
	T* TryGet() const;

	template <typename T>
	bool Has() const;

	template <typename T>
	std::vector<T*> GetAll() const;

	template <typename T>
	void RemoveService(T& service);

	template <typename T>
	void RemoveAll();

private:
	void RemoveFromOwnership(IService* service);
	std::vector<std::unique_ptr<IService>> Services;
	std::unordered_map<std::type_index, std::vector<IService*>> Registry;
};

template <typename T, typename TInterface, typename... Args>
T& ServiceHost::AddService(Args&&... args)
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	static_assert(std::is_base_of<TInterface, T>::value, "T must derive from TInterface");

	auto service = std::make_unique<T>(std::forward<Args>(args)...);
	auto* rawService = service.get();

	Services.emplace_back(std::move(service));

	// Always register under the concrete type
	Registry[std::type_index(typeid(T))].emplace_back(rawService);

	// Additionally register under the interface type if it differs
	if constexpr (!std::is_same_v<T, TInterface>) {
		Registry[std::type_index(typeid(TInterface))].emplace_back(rawService);
	}

	return *rawService;
}

template <typename T>
T& ServiceHost::Get() const
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	if (iter == Registry.end() || iter->second.empty()) {
		throw std::runtime_error("Service not registered: " + std::string(typeid(T).name()));
	}
	return *static_cast<T*>(iter->second.front());
}

template <typename T>
T* ServiceHost::TryGet() const
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	if (iter == Registry.end() || iter->second.empty()) {
		return nullptr;
	}
	return static_cast<T*>(iter->second.front());
}

template <typename T>
bool ServiceHost::Has() const
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	return iter != Registry.end() && !iter->second.empty();
}

template <typename T>
std::vector<T*> ServiceHost::GetAll() const
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	if (iter == Registry.end()) {
		return {};
	}

	std::vector<T*> result;
	result.reserve(iter->second.size());
	for (auto* service : iter->second) {
		result.emplace_back(static_cast<T*>(service));
	}
	return result;
}

template <typename T>
void ServiceHost::RemoveService(T& service)
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	IService* raw = &service;

	for (auto it = Registry.begin(); it != Registry.end(); ) {
		auto& vec = it->second;
		vec.erase(std::remove(vec.begin(), vec.end(), raw), vec.end());
		if (vec.empty()) {
			it = Registry.erase(it);
		} else {
			++it;
		}
	}

	RemoveFromOwnership(raw);
}

template <typename T>
void ServiceHost::RemoveAll()
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	if (iter == Registry.end()) {
		return;
	}

	std::vector<IService*> toRemove = iter->second;
	Registry.erase(iter);

	for (auto& [key, vec] : Registry) {
		vec.erase(std::remove_if(vec.begin(), vec.end(), [&](IService* s) {
			return std::find(toRemove.begin(), toRemove.end(), s) != toRemove.end();
		}), vec.end());
	}

	for (auto* service : toRemove) {
		RemoveFromOwnership(service);
	}
}

inline void ServiceHost::RemoveFromOwnership(IService* service)
{
	Services.erase(std::remove_if(Services.begin(), Services.end(),
		[service](const std::unique_ptr<IService>& s) { return s.get() == service; }),
		Services.end());
}