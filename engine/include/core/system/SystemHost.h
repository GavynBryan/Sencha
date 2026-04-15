#pragma once

#include <core/system/ISystem.h>
#include <core/system/SystemPhase.h>
#include <time/FrameTime.h>
#include <algorithm>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

class SystemHost
{
public:
	template <typename T, typename... Args>
	T& AddSystem(SystemPhase phase, Args&&... args);

	template <typename T>
	T* Get() const;

	template <typename T>
	bool Has() const;

	void Init();
	void Update(const FrameTime& time);
	void Shutdown();

private:
	struct SystemEntry
	{
		SystemPhase Phase = SystemPhase::Update;
		std::unique_ptr<ISystem> System;
	};

	std::vector<SystemEntry> Systems;
	std::unordered_map<std::type_index, ISystem*> Registry;
	bool Initialized = false;

	void SortSystems();
};

template <typename T, typename... Args>
T& SystemHost::AddSystem(SystemPhase phase, Args&&... args)
{
	static_assert(std::is_base_of<ISystem, T>::value, "T must derive from ISystem");

	auto system = std::make_unique<T>(std::forward<Args>(args)...);
	auto* rawSystem = system.get();

	auto typeIndex = std::type_index(typeid(T));
	assert(Registry.find(typeIndex) == Registry.end());
	Registry.emplace(typeIndex, rawSystem);
	Systems.push_back({phase, std::move(system)});

	if (Initialized) {
		SortSystems();
		rawSystem->Init();
	}

	return *rawSystem;
}

template <typename T>
T* SystemHost::Get() const
{
	static_assert(std::is_base_of<ISystem, T>::value, "T must derive from ISystem");
	auto iter = Registry.find(std::type_index(typeid(T)));
	if (iter == Registry.end()) {
		return nullptr;
	}
	return static_cast<T*>(iter->second);
}

template <typename T>
bool SystemHost::Has() const
{
	static_assert(std::is_base_of<ISystem, T>::value, "T must derive from ISystem");
	return Registry.find(std::type_index(typeid(T))) != Registry.end();
}

inline void SystemHost::Init()
{
	SortSystems();
	for (auto& entry : Systems) {
		entry.System->Init();
	}
	Initialized = true;
}

inline void SystemHost::Update(const FrameTime& time)
{
	for (auto& entry : Systems) {
		entry.System->Update(time);
	}
}

inline void SystemHost::Shutdown()
{
	// Shutdown and destroy in reverse order (LIFO). Registry cleared first
	// so any lookups from within a system's destructor can't land on a
	// sibling whose unique_ptr is about to be reset.
	for (auto it = Systems.rbegin(); it != Systems.rend(); ++it) {
		it->System->Shutdown();
	}
	Registry.clear();
	while (!Systems.empty())
	{
		Systems.pop_back();
	}
	Initialized = false;
}

inline void SystemHost::SortSystems()
{
	std::sort(Systems.begin(), Systems.end(),
		[](const SystemEntry& a, const SystemEntry& b)
		{
			return a.Phase < b.Phase;
		});
}