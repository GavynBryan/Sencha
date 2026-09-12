#pragma once

#include <cstdint>

// The icons the editor's controls can show, named for what they depict. A
// tool, a toolbar group, or a panel names an IconId; the chrome draws it from
// its file under editor/icons and tints it with the widget's state. A leaf header with no
// ImGui dependency, so the tool framework can carry an icon id without
// reaching into the UI layer.
enum class IconId : std::uint8_t
{
    None, // the control shows its text label instead

    // Pointing and transforming
    Pointer,
    Move,
    Rotate,
    Scale,
    Resize,
    Pivot,
    Anchor,

    // Shapes and scene objects
    Box,
    Plane,
    Cylinder,
    Light,

    // Grid and snapping
    Grid,
    GridFrame,
    Snap,
    ZoneBounds,

    // Transport and build
    Play,
    Stop,
    Hammer,
    Cancel,
    Check,

    // Browsing and editing
    Folder,
    Search,
    Refresh,
    ChevronDown,
    Add,
    Delete,
    Eye,
    EyeOff,
    Lock,
    Unlock,
    Cut,
    Carve,
    Clip,

    // Mesh element modes
    ModeObject,
    ModeVertex,
    ModeEdge,
    ModeFace,

    // Window caption controls
    WindowMinimize,
    WindowMaximize,
    WindowRestore,
    WindowClose,

    Count,
};
