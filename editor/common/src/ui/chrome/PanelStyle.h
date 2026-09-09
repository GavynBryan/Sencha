#pragma once

// The frame weight a panel asks for. Every weight is the same chamfered metal
// family; the weight sets how much of the panel's area the frame may take.
// Viewport is the quietest (a thin frame and no header rail, so the working
// area stays with the scene); Major is the heaviest, for a panel that should
// dominate its column.
enum class PanelStyle
{
    Standard,
    Major,
    Tool,
    Viewport,
    Compact,
};
