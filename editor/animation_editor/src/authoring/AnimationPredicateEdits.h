#pragma once

#include <anim/AnimFactSchema.h>
#include <anim/AnimPredicate.h>
#include <core/json/JsonValue.h>

#include <cstddef>
#include <string>

void AddAnimPredicateRow(JsonValue::Array& rows, JsonValue test);
bool RemoveAnimPredicateRow(JsonValue::Array& rows, std::size_t row);
// A row is one test or an any-of group. Adding an alternative to a test makes
// a group; removing the second-to-last alternative makes it a test again.
bool AddAnimPredicateAlternative(JsonValue::Array& rows, std::size_t row, JsonValue test);
bool RemoveAnimPredicateAlternative(JsonValue::Array& rows, std::size_t row, std::size_t alternative);

[[nodiscard]] JsonValue MakeAnimFactTest(std::string fact, AnimFactKind kind);
[[nodiscard]] JsonValue MakeAnimTagSetTest(std::string fact, std::string tag);
[[nodiscard]] JsonValue MakeAnimRequestTest(std::string intent);
[[nodiscard]] JsonValue MakeAnimElapsedTest(double seconds);
