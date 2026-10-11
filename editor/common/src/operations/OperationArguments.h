#pragma once

#include <core/json/JsonValue.h>

#include <optional>
#include <span>
#include <string>

// Console tokens to an operation's argument object. Either one JSON5 object,
//   query.near {point: [0, 1, 0], radius: 8}
// or name=value pairs, each value read as JSON5 and otherwise as plain text,
//   query.near point=[0,1,0] radius=8 label=hall
// No tokens is the empty object. Null with `error` set when the tokens are
// neither form.
[[nodiscard]] std::optional<JsonValue> OperationArgumentsFromTokens(
    std::span<const std::string> tokens, std::string& error);
