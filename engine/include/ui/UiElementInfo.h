#pragma once

#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

//=============================================================================
// UiElementInfo
//
// What an inspector can learn about one element of a laid-out document, as
// plain data. The document engine's own element type never crosses this
// boundary; a reference is an opaque ticket the runtime handed out.
//
// A ref resolves to nothing once its element is gone -- removed by a
// model-driven update, replaced by a rebuild after an edit, or its screen
// closed -- rather than to whatever now sits where it was. It is cheap to hold
// across frames; a holder that finds it no longer resolves asks again.
//=============================================================================

struct UiElementRef
{
    UiScreenHandle Screen;
    // A slot in the owning screen's table plus the serial minted into it.
    // Never a position in the tree: positions move when the tree does.
    std::uint32_t Slot = 0;
    std::uint32_t Serial = 0;

    [[nodiscard]] bool IsValid() const { return Screen.IsValid() && Serial != 0; }
    bool operator==(const UiElementRef&) const = default;
};

// The four boxes of the box model, in surface pixels with the origin at the
// surface's top left. Content is what MeasureElement reports.
struct UiElementBoxes
{
    UiElementBox Margin;
    UiElementBox Border;
    UiElementBox Padding;
    UiElementBox Content;
};

struct UiElementInfo
{
    UiElementRef Ref;
    std::string Tag;
    std::string Id;
    std::vector<std::string> Classes;
    std::vector<std::pair<std::string, std::string>> Attributes;
    UiElementBoxes Boxes;
    // Invalid for the document's root element.
    UiElementRef Parent;
    // Root is 0.
    std::uint32_t Depth = 0;
};
