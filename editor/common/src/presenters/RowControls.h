#pragma once

#include <ui/UiScreenDesc.h>

#include <array>
#include <string_view>

// The controls the editor's documents offer for a row (UiRow::Control), beyond
// the runtime's own text, range and choice; editor/ui/controls.rcss and the
// sheet documents draw each by its name.
namespace RowControls
{
using namespace UiRowControls;
inline constexpr std::string_view Toggle = "toggle";     // Value is "true" or "false"
inline constexpr std::string_view Asset = "asset";       // Value names an asset; Parts[0], when set, is how it reads
inline constexpr std::string_view Color = "color";       // Parts[0], when set, is a CSS colour to swatch it with
inline constexpr std::string_view Section = "section";   // a heading the rows after it sit under
inline constexpr std::string_view Action = "action";     // something to do, named by its label, raised by its key
inline constexpr std::string_view LongText = "longtext"; // text that runs over several lines
inline constexpr std::string_view Number = "number";     // typed or dragged; Number, Min and Max when bounded
inline constexpr std::string_view Vector = "vector";     // Parts side by side, Choices naming each
inline constexpr std::string_view Plot = "plot";         // Parts are bar heights, left to right

// Every one, in the order a model's author meets them.
inline constexpr std::array kAll = { Text, Range, Choice, Toggle, Asset, Color, Section, Action, LongText, Number, Vector,
                                     Plot };
} // namespace RowControls
