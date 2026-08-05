#pragma once

#include <core/json/JsonValue.h>
#include <input/InputAction.h>

#include <string>
#include <string_view>
#include <vector>

// Reads authored input data back as prose, so a list of bindings presents as
// the keymap it describes rather than as JSON with expanders.
//
// Pure text synthesis: no ImGui, no document, no engine state. Everything here
// is table-tested, which is also why the exact wording is deliberate -- changing
// a phrase is a test update, not an accident.
//
// Read from the authored JSON rather than from a compiled profile on purpose: a
// document with one broken binding must still show the forty that are fine, and
// compilation stops at the first error.
//
// Text is ASCII on purpose. The editor fonts declare no glyph ranges beyond
// Latin-1, so arrows and typographic operators would not render.

// One action a profile may bind, as declared by an action set.
struct KnownInputAction
{
    std::string Name;
    InputActionType Type = InputActionType::Digital;
    InputActionScope Scope = InputActionScope::Simulation;
};

// The actions declared in an input.actions document's data object. Malformed
// entries are skipped rather than fatal: the vocabulary is edited in the same
// session as the profile that reads it, and one half-typed action should not
// blank the picker.
[[nodiscard]] std::vector<KnownInputAction> CollectInputActions(const JsonValue& actionsData);

// Every action name any binding in a profile's data object refers to, in first
// appearance order, without duplicates.
[[nodiscard]] std::vector<std::string> CollectBoundActionNames(const JsonValue& profileData);

// Action names declared by the set but bound nowhere in the profile.
[[nodiscard]] std::vector<std::string> UnboundInputActions(
    const std::vector<KnownInputAction>& declared,
    const JsonValue& profileData);

// "key.a" -> "A", "key.left_shift" -> "Left Shift", "mouse.delta" ->
// "Mouse movement", "gamepad.south" -> "Gamepad South". A name this does not
// recognize echoes back unchanged, so the author sees exactly what is authored.
[[nodiscard]] std::string DescribeInputControlName(std::string_view controlName);

// What a binding reads as: "WASD", "Arrow keys", "Q / E", "Gamepad South",
// "Mouse movement, scale 0.0025", or "not bound yet".
[[nodiscard]] std::string DescribeInputBinding(const JsonValue& binding);

// "move - WASD", or "Binding" when the binding names no action.
[[nodiscard]] std::string InputBindingCardTitle(const JsonValue& binding);

// "priority 10 - 5 bindings", so a collapsed context still says what it is for.
[[nodiscard]] std::string InputContextCardSubtitle(const JsonValue& context);

// Trims trailing zeros so authored values read as authored: "1", "0.0025".
[[nodiscard]] std::string FormatInputScalar(double value);
