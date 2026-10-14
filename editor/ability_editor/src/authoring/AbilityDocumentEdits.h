#pragma once

#include "authoring/AbilityElements.h"

#include <core/json/JsonValue.h>

#include <cstddef>
#include <string>
#include <string_view>

// Edits to an ability's data object, each addressing elements by key so a
// selection survives reordering. Each is one step of the document's journal
// when the workspace applies it; none reads the World. A new element's key is
// minted fresh, and a failed edit leaves `data` unchanged.

// A phase named `name` after the phase `after`, or last; its key.
[[nodiscard]] std::string AddAbilityPhase(JsonValue& data, std::string name, std::string_view after = {});
// How many transitions name `phase` as where they go.
[[nodiscard]] std::size_t AbilityTransitionsInto(const JsonValue& data, std::string_view phase);
// The phase gone, and every transition that went to it now ending the ability.
bool RemoveAbilityPhase(JsonValue& data, std::string_view phase);
bool MoveAbilityPhase(JsonValue& data, std::string_view phase, std::size_t index);

// `op` placed in `phase`'s `list` at `index` (clamped), keyed afresh; its key.
[[nodiscard]] std::string InsertAbilityOp(JsonValue& data, std::string_view phase, AbilityOpList list, std::size_t index,
                                          JsonValue op);
// An op moved, within its list or to another phase's list.
bool MoveAbilityOp(JsonValue& data, std::string_view op, std::string_view phase, AbilityOpList list, std::size_t index);

// A transition out of `phase` on `trigger` (empty: as soon as its ops have run)
// to the phase `to` or "end"; its key.
[[nodiscard]] std::string AddAbilityTransition(JsonValue& data, std::string_view phase, std::string_view trigger,
                                               std::string_view to);
[[nodiscard]] std::string AddAbilityLocal(JsonValue& data, std::string name, std::string_view type);

// `condition`, keyed afresh, as the guard (`when`) of the op or transition `element`;
// its key. A null condition takes the guard away.
std::string SetAbilityGuard(JsonValue& data, std::string_view element, JsonValue condition);

// `value` as the member `member` of the element `element`.
bool SetAbilityElementMember(JsonValue& data, std::string_view element, std::string_view member, JsonValue value);

// Any keyed element taken out of the list that holds it.
bool RemoveAbilityElement(JsonValue& data, std::string_view element);
// A copy of an element just after it, with every key inside it minted afresh; its key.
[[nodiscard]] std::string DuplicateAbilityElement(JsonValue& data, std::string_view element);

// Every "key" member under `value` given a fresh key.
void RemintAbilityKeys(JsonValue& value);
