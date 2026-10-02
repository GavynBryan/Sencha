#pragma once

#include <cstdint>

// The icons the editor's controls can show, named for what they depict. A
// tool, a toolbar group, or a panel names an IconId; the chrome draws it from
// its file under editor/ui/icons and tints it with the widget's state. A leaf
// header, so the tool framework can carry an icon id without reaching into the
// UI layer.
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
    World, // the gizmo along world axes
    Local, // the gizmo along the selection's own axes

    // Shapes and scene objects
    Box,
    Plane,
    Cylinder,
    Light,
    Camera,

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
    ClipFront, // keep the front half
    ClipBack,  // keep the back half
    ClipSplit, // keep both, as two brushes
    ClipCap,   // close the cut with a face

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
