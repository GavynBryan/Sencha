#pragma once

#include <core/json/JsonValue.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

//=============================================================================
// JsonParse
//
// Recursive descent JSON parser. Accepts a string_view and returns a
// JsonValue on success, or std::nullopt with error details on failure.
//
// This is a load-time utility â€” not optimized for streaming or
// incremental parsing. Intended for config files, not runtime data.
//=============================================================================

struct JsonParseError
{
	std::string Message;
	std::size_t Position = 0;
};

std::optional<JsonValue> JsonParse(std::string_view input, JsonParseError* error = nullptr);

// JsonParse over a file's contents. Nullopt when the file cannot be read or
// does not parse; `error` says which, with the parse position folded into the
// message. The read-then-parse triple every JSON-consuming loader had been
// writing inline.
std::optional<JsonValue> JsonParseFile(const std::filesystem::path& path,
                                       std::string* error = nullptr);
