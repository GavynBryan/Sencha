#pragma once

#include <input/InputActionRegistry.h>
#include <input/InputConfig.h>
#include <input/InputBindingTable.h>
#include <json/JsonValue.h>
#include <optional>
#include <string>
#include <string_view>

//=============================================================================
// Input binding compiler
//
// Two-phase pipeline:
//   1. DeserializeInputConfig: JSON DOM -> typed config structs
//   2. CompileInputBindings:   config structs -> runtime binding table
//
// Generic string resolution (device names, trigger names) happens here at
// load time. App-specific action resolution and backend-specific control
// resolution are supplied through cold-path resolver interfaces.
//=============================================================================

struct InputCompileError
{
	std::string Message;
};

class IInputControlResolver
{
public:
	virtual ~IInputControlResolver() = default;

	[[nodiscard]] virtual std::optional<uint16_t> ResolveControl(
		InputDeviceType device,
		std::string_view control) const = 0;
};

// Parse a JsonValue tree into typed config structs.
std::optional<InputConfigData> DeserializeInputConfig(
	const JsonValue& root,
	InputCompileError* error = nullptr);

// Compile config structs into a runtime binding table.
std::optional<InputBindingTable> CompileInputBindings(
	const InputConfigData& config,
	const IInputActionResolver& actionResolver,
	const IInputControlResolver& controlResolver,
	InputCompileError* error = nullptr);
