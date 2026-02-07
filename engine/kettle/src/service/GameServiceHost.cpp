#include <service/GameServiceHost.h>

template <typename T, typename TInterface, typename... Args>
T& GameServiceHost::AddService(Args&&... args)
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	static_assert(std::is_base_of<TInterface, T>::value, "T must derive from TInterface");

	auto service = std::make_unique<T>(std::forward<Args>(args)...);
	service->SetHost(this);
	auto* rawService = service.get();

	using SelectedType = std::conditional_t<std::is_same_v<T, TInterface>, T, TInterface>;
	std::type_index typeIndex = std::type_index(typeid(SelectedType));

	Services.emplace_back(std::move(service));
	Registry[typeIndex].emplace_back(rawService);

	return *rawService;
}

template <typename T>
T& GameServiceHost::Get() const
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	if (iter == Registry.end() || iter->second.empty()) {
		throw std::runtime_error("Service not registered: " + std::string(typeid(T).name()));
	}
	return *static_cast<T*>(iter->second.front());
}

template <typename T>
T* GameServiceHost::TryGet() const
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	if (iter == Registry.end() || iter->second.empty()) {
		return nullptr;
	}
	return static_cast<T*>(iter->second.front());
}

template <typename T>
bool GameServiceHost::Has() const
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	return iter != Registry.end() && !iter->second.empty();
}

template <typename T>
void GameServiceHost::RemoveService(T& service)
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	IService* raw = &service;

	// Remove the pointer from all registry vectors that reference it
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
void GameServiceHost::RemoveAll()
{
	static_assert(std::is_base_of<IService, T>::value, "T must derive from IService");
	auto iter = Registry.find(std::type_index(typeid(T)));
	if (iter == Registry.end()) {
		return;
	}

	// Copy the pointers before modifying the registry
	std::vector<IService*> toRemove = iter->second;
	Registry.erase(iter);

	// Also scrub these pointers from any other registry entries
	for (auto& [key, vec] : Registry) {
		vec.erase(std::remove_if(vec.begin(), vec.end(), [&](IService* s) {
			return std::find(toRemove.begin(), toRemove.end(), s) != toRemove.end();
		}), vec.end());
	}

	for (auto* service : toRemove) {
		RemoveFromOwnership(service);
	}
}

void GameServiceHost::RemoveFromOwnership(IService* service)
{
	Services.erase(std::remove_if(Services.begin(), Services.end(),
		[service](const std::unique_ptr<IService>& s) { return s.get() == service; }),
		Services.end());
}

template <typename T>
std::vector<T*> GameServiceHost::GetAll() const
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