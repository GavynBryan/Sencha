#pragma once

#include "tools/ITool.h"

#include <math/Vec.h>

class LevelTarget;

// Makes a space by dragging its footprint on a floor: the grid or a surface
// facing up. It stands the project's space height tall and commits on release
// through CreateSpace, so it is the same space space.create makes.
class SpaceTool final : public ITool
{
public:
    explicit SpaceTool(LevelTarget& level);

    [[nodiscard]] std::string_view GetId() const override { return "space"; }
    [[nodiscard]] std::string_view GetDisplayName() const override { return "Space"; }
    [[nodiscard]] IconId GetIcon() const override { return IconId::Box; }
    [[nodiscard]] Shortcut GetShortcut() const override;
    [[nodiscard]] bool UsesTransformGizmo() const override { return false; }
    [[nodiscard]] std::string Hint(const ToolContext& ctx) const override;

    std::unique_ptr<IInteraction> BeginDrag(ToolContext& ctx, EditorViewport& viewport,
                                            const PointerEvent& pressPointer) override;

    // The footprint's opposite corners on the floor, as the drag left them.
    void Create(ToolContext& ctx, Vec3d corner, Vec3d opposite);

private:
    LevelTarget& Level;
};
