#pragma once

#include <math/geometry/3d/Ray3d.h>
#include "../selection/SelectableRef.h"

#include <imgui.h>

#include <optional>

struct EditorViewport;
class LevelScene;

class PickingService
{
public:
    [[nodiscard]] SelectableRef Pick(const EditorViewport& viewport,
                                     ImVec2 point,
                                     const LevelScene& scene) const;
    [[nodiscard]] std::optional<Vec3d> ProjectPointToGrid(const EditorViewport& viewport,
                                                          ImVec2 point) const;

private:
    [[nodiscard]] Ray3d BuildRay(const EditorViewport& viewport, ImVec2 point) const;
};
