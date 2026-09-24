#pragma once

#include <core/json/JsonValue.h>

#include <cstddef>
#include <string>
#include <string_view>

//=============================================================================
// Flow edits
//
// The operations the Flow panel offers over a flow document's root, as pure
// edits of the authored JSON applied inside a document transaction, one undo
// step each. None of them can make control go backward: a branch is added
// only to a later section, and a move that would put a branch's target at or
// before its section is refused. Removing a section removes the branches to
// it and, when it was the cancel section, the cancel.
//
// Loop and branch conditions are predicates, edited with the predicate edits
// over the rows the accessors below return. A new while loop or branch starts
// with an empty condition for the author to fill in, which the document
// reports until they do.
//=============================================================================

[[nodiscard]] JsonValue::Array* AnimFlowSections(JsonValue& root);

void AddAnimFlowSection(JsonValue& root, std::string tag, std::string clip);
bool RemoveAnimFlowSection(JsonValue& root, std::size_t section);
bool MoveAnimFlowSection(JsonValue& root, std::size_t from, std::size_t to);

// "once", "while" or "count".
bool SetAnimFlowLoop(JsonValue& root, std::size_t section, std::string_view loop);
bool SetAnimFlowEnds(JsonValue& root, std::size_t section, bool ends);
bool SetAnimFlowCancelImmediately(JsonValue& root, std::size_t section, bool immediately);
// An empty tag clears the cancel section: a cancel then ends the flow.
bool SetAnimFlowCancel(JsonValue& root, std::string_view tag);

// False, and nothing changes, when `to` is not a later section.
bool AddAnimFlowBranch(JsonValue& root, std::size_t section, std::size_t to);
bool RemoveAnimFlowBranch(JsonValue& root, std::size_t section, std::size_t branch);

// A while loop's condition rows, created empty; null unless the section loops
// while something holds.
[[nodiscard]] JsonValue::Array* AnimFlowLoopCondition(JsonValue& root, std::size_t section);
// A branch's condition rows, created empty; null for a branch that is not there.
[[nodiscard]] JsonValue::Array* AnimFlowBranchCondition(JsonValue& root, std::size_t section, std::size_t branch);
