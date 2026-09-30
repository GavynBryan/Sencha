#include "icons/IconNames.h"

std::string_view IconName(IconId id)
{
    switch (id)
    {
    case IconId::Pointer: return "pointer";
    case IconId::Move: return "move";
    case IconId::Rotate: return "rotate";
    case IconId::Scale: return "scale";
    case IconId::Resize: return "resize";
    case IconId::Pivot: return "pivot";
    case IconId::Anchor: return "anchor";
    case IconId::Box: return "box";
    case IconId::Plane: return "plane";
    case IconId::Cylinder: return "cylinder";
    case IconId::Light: return "light";
    case IconId::Camera: return "camera";
    case IconId::Grid: return "grid";
    case IconId::GridFrame: return "grid-frame";
    case IconId::Snap: return "snap";
    case IconId::ZoneBounds: return "zone-bounds";
    case IconId::Play: return "play";
    case IconId::Stop: return "stop";
    case IconId::Hammer: return "hammer";
    case IconId::Cancel: return "cancel";
    case IconId::Check: return "check";
    case IconId::Folder: return "folder";
    case IconId::Search: return "search";
    case IconId::Refresh: return "refresh";
    case IconId::ChevronDown: return "chevron-down";
    case IconId::Add: return "add";
    case IconId::Delete: return "delete";
    case IconId::Eye: return "eye";
    case IconId::EyeOff: return "eye-off";
    case IconId::Lock: return "lock";
    case IconId::Unlock: return "unlock";
    case IconId::Cut: return "cut";
    case IconId::Carve: return "carve";
    case IconId::Clip: return "clip";
    case IconId::ClipFront: return "clip-front";
    case IconId::ClipBack: return "clip-back";
    case IconId::ClipSplit: return "clip-split";
    case IconId::ClipCap: return "clip-cap";
    case IconId::ModeObject: return "mode-object";
    case IconId::ModeVertex: return "mode-vertex";
    case IconId::ModeEdge: return "mode-edge";
    case IconId::ModeFace: return "mode-face";
    case IconId::WindowMinimize: return "window-minimize";
    case IconId::WindowMaximize: return "window-maximize";
    case IconId::WindowRestore: return "window-restore";
    case IconId::WindowClose: return "window-close";
    case IconId::None:
    case IconId::Count:
        break;
    }
    return {};
}
