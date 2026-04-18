#pragma once

#include <platform/InputActionRegistry.h>
#include <platform/InputConfig.h>
#include <platform/InputBindingTable.h>
#include <platform/SdlInputControlResolver.h>
#include <core/json/JsonValue.h>
#include <optional>
#include <string>

//=============================================================================
// Input binding compiler
//
// Two-phase pipeline:
//   1. DeserializeInputConfig: JSON DOM -> typed config structs
//   2. CompileInputBindings:   config structs -> runtime binding table
//
// Generic string resolution (device names, trigger names) happens here at
// load time. Action resolution is supplied through IInputActionResolver;
// control resolution uses SdlInputControlResolver directly.
//=============================================================================

struct InputCompileError
{
	std::string Message;
};

// Parse a JsonValue tree into typed config structs.
std::optional<InputConfigData> DeserializeInputConfig(
	const JsonValue& root,
	InputCompileError* error = nullptr);

// Compile config structs into a runtime binding table.
std::optional<InputBindingTable> CompileInputBindings(
	const InputConfigData& config,
	const IInputActionResolver& actionResolver,
	const SdlInputControlResolver& controlResolver,
	InputCompileError* error = nullptr);
