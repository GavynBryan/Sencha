#pragma once

class JsonValue;

// Objects compare by member name in any order; arrays in order.
[[nodiscard]] bool JsonEquals(const JsonValue& a, const JsonValue& b);
