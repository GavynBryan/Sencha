#pragma once

#include <core/json/JsonValue.h>

#include <cstddef>
#include <string>
#include <string_view>

// Edits of a selector document's root; each leaves a valid document valid.

[[nodiscard]] JsonValue::Array* AnimSelectorRules(JsonValue& root);

void AddAnimSelectorRule(JsonValue& root, std::string name, std::string behavior, int priority);
bool RemoveAnimSelectorRule(JsonValue& root, std::size_t rule);
// Priority, not position, orders evaluation, so this only reorders rules of equal priority.
bool MoveAnimSelectorRule(JsonValue& root, std::size_t from, std::size_t to);

// Adding copies the enter rows; without a stay the rule stays on enter.
bool SetAnimSelectorStay(JsonValue& root, std::size_t rule, bool present);

// Rows are created empty when absent; null for a missing rule or unknown field.
[[nodiscard]] JsonValue::Array* AnimSelectorPredicate(JsonValue& root, std::size_t rule, std::string_view field);
