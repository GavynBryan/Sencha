#pragma once

#include "../selection/SelectableRef.h"

#include <imgui.h>

struct EditorViewport;

class PickingService
{
public:
    [[nodiscard]] SelectableRef Pick(const EditorViewport& viewport, ImVec2 point) const;
};
