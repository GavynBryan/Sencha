#pragma once

#include <core/json/JsonValue.h>

#include <optional>
#include <string>
#include <string_view>

// The text after an operation's name, as its argument object. Either one JSON5
// object,
//   query.near {point: [0, 1, 0], radius: 8}
// or name=value pairs split at whitespace outside quotes and brackets, each
// value read as JSON5 and otherwise as plain text,
//   query.near point=[0, 1, 0] radius=8 label='east hall'
// Empty text is the empty object. Null with `error` set when the text is
// neither form.
[[nodiscard]] std::optional<JsonValue> OperationArgumentsFromText(std::string_view text,
                                                                  std::string& error);
