#pragma once

#include <string>
#include <vector>

//=============================================================================
// Input config types
//
// Authored config structs — deserialized from JSON at load time, then
// compiled into runtime binding tables. Actions are optional diagnostics;
// stable action IDs come from the app-owned InputActionRegistry.
// These types exist only during loading. The runtime input pipeline uses
// InputBindingTable with numeric IDs and flat lookup arrays.
//=============================================================================

struct InputActionConfig
{
	std::string Name;
};

struct InputBindingConfig
{
	std::string Action;
	std::string Device;
	std::string Control;
	std::string Trigger;
};

struct InputContextConfig
{
	std::string Name;
	std::vector<std::string> Actions;
};

struct InputConfigData
{
	std::vector<InputActionConfig> Actions;
	std::vector<InputBindingConfig> Bindings;
	std::vector<InputContextConfig> Contexts;
};
