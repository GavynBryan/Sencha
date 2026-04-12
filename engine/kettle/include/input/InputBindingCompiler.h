#pragma once

#include <input/InputConfig.h>
#include <input/InputBindingTable.h>
#include <json/JsonValue.h>
#include <optional>
#include <string>

//=============================================================================
// Input binding compiler
//
// Two-phase pipeline:
//   1. DeserializeInputConfig: JSON DOM -> typed config structs
//   2. CompileInputBindings:   config structs -> runtime binding table
//
// All string resolution (action names, key names, device names, trigger
// names) happens here at load time. The resulting InputBindingTable
// contains only numeric IDs and flat lookup arrays.
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
	InputCompileError* error = nullptr);
