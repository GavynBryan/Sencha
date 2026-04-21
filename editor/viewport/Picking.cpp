#include "Picking.h"

#include "EditorViewport.h"

SelectableRef PickingService::Pick(const EditorViewport& viewport, ImVec2 point) const
{
    (void)viewport;
    (void)point;
    return {};
}
