#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// The two sheets a window's shell presents: the tool in hand's card, and the
// selection's card or drawer.
enum class SheetId : std::uint8_t
{
    Tool,
    Selection,
};

// As the shell's document names them.
[[nodiscard]] inline std::string_view SheetName(SheetId sheet)
{
    return sheet == SheetId::Tool ? "tool" : "selection";
}

[[nodiscard]] inline std::optional<SheetId> SheetNamed(std::string_view name)
{
    if (name == "tool")
        return SheetId::Tool;
    if (name == "selection")
        return SheetId::Selection;
    return std::nullopt;
}

// A row of one sheet, and which of a vector's numbers, if one.
struct SheetPoint
{
    SheetId Sheet = SheetId::Tool;
    std::size_t Row = 0;
    std::int64_t Element = -1;

    friend bool operator==(const SheetPoint&, const SheetPoint&) = default;
};
