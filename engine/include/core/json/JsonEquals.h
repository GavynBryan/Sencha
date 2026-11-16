#pragma once

class JsonValue;

// Value equality: numbers by value, arrays in order, objects by member name in
// any order.
[[nodiscard]] bool JsonEquals(const JsonValue& a, const JsonValue& b);
