#pragma once

#include <core/json/JsonValue.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What a keyed element of an ability's data is, by where it sits.
enum class AbilityElementKind : std::uint8_t
{
    Condition,
    Local,
    CostGroup,
    Phase,
    Op,
    Presentation,
    Transition,
};

// The lists of a phase that hold ops; While is the list under `while`.
enum class AbilityOpList : std::uint8_t
{
    Enter,
    While,
    Hold,
    Exit,
};

[[nodiscard]] std::string_view AbilityOpListName(AbilityOpList list);
[[nodiscard]] std::optional<AbilityOpList> AbilityOpListNamed(std::string_view name);

// One element of an ability as the document holds it now. Selection, undo and
// problems name an element by Key; Path is where it is until the next edit.
struct AbilityElement
{
    AbilityElementKind Kind = AbilityElementKind::Phase;
    std::string Key;
    // As data properties and diagnostics name it: "$.data.phases[1].enter[0]".
    std::string Path;
    // The phase it is in, empty outside one.
    std::string Phase;
    // The member holding it: "enter", "then", "locals", "when", ...
    std::string List;
    std::size_t Index = 0;
};

// Every keyed element under an ability's data object, in document order.
[[nodiscard]] std::vector<AbilityElement> AbilityElementsOf(const JsonValue& data);
[[nodiscard]] std::optional<AbilityElement> FindAbilityElement(const JsonValue& data, std::string_view key);
// The innermost element at or above `path`: what a problem at that path is about.
[[nodiscard]] std::optional<AbilityElement> AbilityElementAt(const JsonValue& data, std::string_view path);

// What an element is called where it is listed: a phase's or local's name, an
// op's verb, query or lease, a transition's trigger and target.
[[nodiscard]] std::string AbilityElementLabel(const JsonValue& data, const AbilityElement& element);
