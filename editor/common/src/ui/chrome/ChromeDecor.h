#pragma once

#include <string_view>

// Flavor copy in the space a panel knows is free. A panel names the slot it
// is showing (an empty hierarchy, an idle tool panel); the text comes from
// the theme's "decor" strings, so the words change in data, never in code.
namespace EditorChrome
{
enum class DecorSlot
{
    HierarchyEmpty,
    MaterialBrowserEmpty,
    SceneBrowserEmpty,
    ToolPropertiesIdle,
    ConsoleEmpty,
    StatusTagline,
};

// The slot's text (lines separated by newlines); empty when the theme
// silenced it.
[[nodiscard]] std::string_view DecorText(DecorSlot slot);

// Draws the slot's lines centered in the remaining content region, low
// contrast, without consuming layout. Draws nothing in a region too small to
// hold them or when the slot is silent.
void EmptyRegionLabel(DecorSlot slot);
} // namespace EditorChrome
