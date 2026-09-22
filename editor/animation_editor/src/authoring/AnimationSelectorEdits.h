#pragma once

#include <anim/AnimFactSchema.h>
#include <anim/AnimPredicate.h>
#include <core/json/JsonValue.h>

#include <cstddef>
#include <string>
#include <string_view>

//=============================================================================
// Selector edits
//
// The operations the rule table and predicate builder offer, over a selector
// document's root. Each one is a pure edit of the authored JSON that leaves it
// valid if it was valid: the panels apply them inside a document transaction,
// so each is one undo step, and the tests drive them without a GUI.
//
// A predicate field is "enter" or "stay". A row is one test or an any-of
// group; adding an alternative to a single test turns it into a group, and
// removing the last-but-one alternative turns the group back into a test.
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

bool AddAnimPredicateRow(JsonValue& root, std::size_t rule, std::string_view field, JsonValue test);
bool RemoveAnimPredicateRow(JsonValue& root, std::size_t rule, std::string_view field, std::size_t row);
bool AddAnimPredicateAlternative(JsonValue& root, std::size_t rule, std::string_view field, std::size_t row,
                                 JsonValue test);
bool RemoveAnimPredicateAlternative(JsonValue& root, std::size_t rule, std::string_view field, std::size_t row,
                                    std::size_t alternative);

// The test a builder row starts from, for each kind of thing a test reads.
[[nodiscard]] JsonValue MakeAnimFactTest(std::string fact, AnimFactKind kind);
[[nodiscard]] JsonValue MakeAnimTagSetTest(std::string fact, std::string tag);
[[nodiscard]] JsonValue MakeAnimRequestTest(std::string intent);
[[nodiscard]] JsonValue MakeAnimElapsedTest(double seconds);
