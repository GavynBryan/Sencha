#pragma once

#include "tools/ITool.h"
#include "viewport/ViewportId.h"

#include <math/Vec.h>

#include <optional>
#include <string>
#include <vector>

//=============================================================================
// PlaceTool
//
// Stamps what it places where the pointer points: on the nearest surface, or on
// the working grid where the ray misses everything. Its variants are the prefab
// in hand, and every component that declares itself placeable (a light, a
// player start), which is placed as a new entity carrying just that component.
// While it hovers, a cross marks the spot; each click places one, turned by the
// tool's yaw, as one undoable step with it selected.
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
    [[nodiscard]] std::span<const Variant> GetVariants() const override;
    [[nodiscard]] int GetActiveVariant(const ToolContext& ctx) const override;
    void SelectVariant(ToolContext& ctx, std::size_t index) override;

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
    void RefreshKinds() const;
    [[nodiscard]] const std::string* ChosenLabel() const;

    // The prefab in hand has no component key.
    struct PlaceKind
    {
        std::string Label;
        std::string ComponentKey;
    };
    mutable std::vector<PlaceKind> Kinds;
    mutable std::vector<Variant> KindChoices;
    std::string Chosen;
    int YawDegrees = 0;
};
