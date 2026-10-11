#pragma once

#include "tools/ITool.h"
#include "viewport/ViewportId.h"

#include <math/Vec.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

//=============================================================================
// PlaceTool
//
// Stamps what it places where the pointer points: on the nearest surface, or on
// the working grid where the ray misses everything. Its variants are the prefab
// in hand, an empty entity, and every component that declares itself placeable
// (a light, a player start), placed as a new entity carrying just that component.
// While it hovers, a cross marks the spot; each click places one, turned by the
// tool's yaw, as one undoable step with it selected.
//=============================================================================
class PlaceTool final : public ITool
{
public:
    [[nodiscard]] std::string_view GetId() const override { return "place"; }
    [[nodiscard]] std::string_view GetDisplayName() const override { return "Entity"; }
    [[nodiscard]] IconId GetIcon() const override { return IconId::Pawn; }
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

    enum class PlaceSource : std::uint8_t
    {
        Prefab,
        Empty,
        Component,
    };
    struct PlaceChoice
    {
        PlaceSource Source = PlaceSource::Prefab;
        std::string ComponentKey = {};
        bool operator==(const PlaceChoice&) const = default;
    };
    struct PlaceKind
    {
        std::string Label;
        PlaceChoice What;
        IconId Icon = IconId::None;
    };
    mutable std::vector<PlaceKind> Kinds;
    mutable std::vector<Variant> KindChoices;
    PlaceChoice Chosen;
    int YawDegrees = 0;
};
