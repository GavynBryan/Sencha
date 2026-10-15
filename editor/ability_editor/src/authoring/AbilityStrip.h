#pragma once

#include <abilities/AbilityDeclaration.h>
#include <core/json/JsonValue.h>
#include <ui/UiScreenDesc.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

// The open ability as the phase strip shows it: flat lists the strip document
// repeats over, each row keyed by its element and naming what holds it in
// Value, so a card shows its slots and a slot its ops. Parts[0] is a guard's
// text; Parts[1] is the worst problem's severity ("error", "warning", "info").
struct AbilityStripRows
{
    // A phase: its name, its clock and the transitions into it in Detail.
    std::vector<UiRow> Cards;
    // A phase's op list, keyed by AbilityStripSlotKey: its title; Detail "fixed" for the
    // list of what it presents, which takes no ops.
    std::vector<UiRow> Slots;
    // An op: what it runs, its kind in Detail.
    std::vector<UiRow> Ops;
    // A transition: its trigger and where it goes.
    std::vector<UiRow> Links;
};

// A card is Expanded while what is selected is inside it.
[[nodiscard]] AbilityStripRows AbilityStripOf(const JsonValue& data, std::span<const AbilityDiagnostic> diagnostics,
                                              std::string_view selected);

[[nodiscard]] std::string AbilityStripSlotKey(std::string_view phase, std::string_view list);

// The element `carried` dropped on `target`: a phase on a phase goes before it;
// an op on an op goes before it, on a slot to its end, and on a phase to the end
// of its entry list. False, and `data` unchanged, for anything else.
bool DropAbilityElement(JsonValue& data, std::string_view carried, std::string_view target);
