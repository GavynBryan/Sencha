#include <system/SystemHost.h>
#include <algorithm>

void SystemHost::Init()
{
	SortSystems();
	for (auto& entry : Systems) {
		entry.System->Init();
	}
	Initialized = true;
}

void SystemHost::Update()
{
	for (auto& entry : Systems) {
		entry.System->Update();
	}
}

void SystemHost::Shutdown()
{
	// Shutdown in reverse order
	for (auto it = Systems.rbegin(); it != Systems.rend(); ++it) {
		it->System->Shutdown();
	}
	Systems.clear();
	Registry.clear();
	Initialized = false;
}

void SystemHost::SortSystems()
{
	std::sort(Systems.begin(), Systems.end(),
		[](const SystemEntry& a, const SystemEntry& b) {
			return a.Order < b.Order;
		}
	);
}
