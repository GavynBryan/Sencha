#pragma once

#include <anim/AnimFactSchema.h>
#include <anim/AnimPredicate.h>
#include <core/json/JsonValue.h>

#include <cstddef>
#include <string>

//=============================================================================
// Predicate edits
//
// The operations the predicate builder offers, over one predicate's rows in a
// document -- a selector rule's enter or stay, a flow's loop condition or
// branch condition. Pure edits of the authored JSON that the panels apply
// inside a document transaction, one undo step each.
//
// A row is one test or an any-of group; adding an alternative to a single
// test turns it into a group, and removing the last-but-one alternative turns
// the group back into a test.
//=============================================================================

void AddAnimPredicateRow(JsonValue::Array& rows, JsonValue test);
bool RemoveAnimPredicateRow(JsonValue::Array& rows, std::size_t row);
bool AddAnimPredicateAlternative(JsonValue::Array& rows, std::size_t row, JsonValue test);
bool RemoveAnimPredicateAlternative(JsonValue::Array& rows, std::size_t row, std::size_t alternative);

// The test a builder row starts from, for each kind of thing a test reads.
[[nodiscard]] JsonValue MakeAnimFactTest(std::string fact, AnimFactKind kind);
[[nodiscard]] JsonValue MakeAnimTagSetTest(std::string fact, std::string tag);
[[nodiscard]] JsonValue MakeAnimRequestTest(std::string intent);
[[nodiscard]] JsonValue MakeAnimElapsedTest(double seconds);
