#pragma once

#include <json/JsonValue.h>
#include <optional>
#include <string>
#include <string_view>

//=============================================================================
// JsonParse
//
// Recursive descent JSON parser. Accepts a string_view and returns a
// JsonValue on success, or std::nullopt with error details on failure.
//
// This is a load-time utility — not optimized for streaming or
// incremental parsing. Intended for config files, not runtime data.
//=============================================================================

struct JsonParseError
{
	std::string Message;
	std::size_t Position = 0;
};

std::optional<JsonValue> JsonParse(std::string_view input, JsonParseError* error = nullptr);
