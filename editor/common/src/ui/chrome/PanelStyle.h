#pragma once

// The chrome composition a panel asks for. Not a weight: each value names a
// whole look -- how heavy the frame is, where its ornaments mount, which header
// plate it carries -- and the chrome kit knows what each one implies. A panel
// names one and never learns the rest.
//
// Viewport is the quietest (a thin frame and no header rail, so the working
// area stays with the scene). ViewportPrimary is the scene view the editor is
// built around: a machined bezel heavy enough to make it the eye's first stop.
enum class PanelStyle
{
    Standard,
    Tool,
    Viewport,
    ViewportPrimary,
    Compact,

    Count, // keep last: bounds the spec table
};
