#pragma once

#include "tools/ITool.h"
#include "viewport/ViewportId.h"

#include <math/Vec.h>

#include <optional>

//=============================================================================
// PlaceTool
//
// Stamps copies of the prefab in hand where the pointer points: on the
// nearest surface, or on the working grid where the ray misses everything.
// While it hovers, a cross marks the spot; each click places one instance,
// turned by the tool's yaw, as one undoable step with its root selected.
//=============================================================================
class PlaceTool final : public ITool
{
public:
    [[nodiscard]] std::string_view GetId() const override { return "place"; }
    [[nodiscard]] std::string_view GetDisplayName() const override { return "Place"; }
    [[nodiscard]] IconId GetIcon() const override { return IconId::Box; }
    [[nodiscard]] Shortcut GetShortcut() const override;
    [[nodiscard]] bool UsesTransformGizmo() const override { return false; }
    [[nodiscard]] std::span<const ResourceRole> Resources() const override;

    void OnDeactivate(ToolContext& ctx) override;
    InputConsumed OnHover(ToolContext& ctx, EditorViewport& viewport, Vec2d pos) override;
    void OnHoverEnd(ToolContext& ctx) override;
    InputConsumed OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;

    [[nodiscard]] std::vector<PropertyNode> Parameters(const ToolContext& ctx) const override;
    PropertyEditResult SetParameter(ToolContext& ctx, std::string_view key, std::string_view value) override;
    [[nodiscard]] std::string Hint(const ToolContext& ctx) const override;

private:
    [[nodiscard]] static std::optional<Vec3d> Target(ToolContext& ctx, const EditorViewport& viewport, Vec2d pos);
    void ShowGhost(ToolContext& ctx, Vec3d point);
    static void ClearGhost(ToolContext& ctx);

    int YawDegrees = 0;
};
