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

class GameServiceHost
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