#pragma once

#include <core/json/JsonValue.h>

#include <cstddef>
#include <string>
#include <string_view>

// Edits of a flow document's data object. No edit can make control go backward:
// branches only target later sections, and a move that would reverse one is refused.

[[nodiscard]] JsonValue::Array* AnimFlowSections(JsonValue& data);

void AddAnimFlowSection(JsonValue& data, std::string tag, std::string clip);
// Also removes branches to the section, and the cancel when it was the cancel section.
bool RemoveAnimFlowSection(JsonValue& data, std::size_t section);
bool MoveAnimFlowSection(JsonValue& data, std::size_t from, std::size_t to);

// "once", "while" or "count".
bool SetAnimFlowLoop(JsonValue& data, std::size_t section, std::string_view loop);
bool SetAnimFlowEnds(JsonValue& data, std::size_t section, bool ends);
bool SetAnimFlowCancelImmediately(JsonValue& data, std::size_t section, bool immediately);
// An empty tag clears the cancel section: a cancel then ends the flow.
bool SetAnimFlowCancel(JsonValue& data, std::string_view tag);

// False, and nothing changes, when `to` is not a later section.
bool AddAnimFlowBranch(JsonValue& data, std::size_t section, std::size_t to);
bool RemoveAnimFlowBranch(JsonValue& data, std::size_t section, std::size_t branch);

// Condition rows are created empty when absent. Null unless the section is a while loop.
[[nodiscard]] JsonValue::Array* AnimFlowLoopCondition(JsonValue& data, std::size_t section);
[[nodiscard]] JsonValue::Array* AnimFlowBranchCondition(JsonValue& data, std::size_t section, std::size_t branch);
