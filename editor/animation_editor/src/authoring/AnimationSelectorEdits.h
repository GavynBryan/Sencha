#pragma once

#include <core/json/JsonValue.h>

#include <cstddef>
#include <string>
#include <string_view>

//=============================================================================
// Selector edits
//
// The operations the rule table offers, over a selector document's root. Each
// one is a pure edit of the authored JSON that leaves it valid if it was
// valid: the panels apply them inside a document transaction, so each is one
// undo step, and the tests drive them without a GUI. A rule's predicates are
// edited with the predicate edits, over the rows AnimSelectorPredicate
// returns.
//=============================================================================

[[nodiscard]] JsonValue::Array* AnimSelectorRules(JsonValue& root);

void AddAnimSelectorRule(JsonValue& root, std::string name, std::string behavior, int priority);
bool RemoveAnimSelectorRule(JsonValue& root, std::size_t rule);
// Moves a rule within the list; priority, not position, orders evaluation, so
// this reorders only rules of equal priority in effect.
bool MoveAnimSelectorRule(JsonValue& root, std::size_t from, std::size_t to);

// A rule's own stay predicate: added as a copy of its enter, removed to stay
// on enter again.
bool SetAnimSelectorStay(JsonValue& root, std::size_t rule, bool present);

// A rule's "enter" or "stay" rows, created empty when the rule has none; null
// for a rule or field that is not there.
[[nodiscard]] JsonValue::Array* AnimSelectorPredicate(JsonValue& root, std::size_t rule, std::string_view field);
