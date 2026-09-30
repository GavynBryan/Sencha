#pragma once

#include <ui/UiElementInfo.h>

#include <cstdint>

// What the panels agree on about how the document is being looked at. Owned by
// the composition root, read and written by the panels that present it: the
// Preview marks what is under the pointer, the Outline and Element panels
// follow the selection, the Model panel asks for the Bindings tab.
struct PreviewViewState
{
    enum class Zoom : std::uint8_t { Fit, Half, Actual, Double };

    UiElementRef Hovered;
    UiElementRef Selected;
    Zoom ZoomLevel = Zoom::Fit;
    bool SafeArea = false;
    bool EditorTheme = false;
    // Set by a diagnostics row; cleared by the Model panel once it has shown
    // the tab.
    bool ShowBindings = false;
};
