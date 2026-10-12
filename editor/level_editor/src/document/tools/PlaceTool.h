#pragma once

#include "document/placement/PendingPlacement.h"
#include "tools/ITool.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Puts down the prefab in hand, an empty entity, or one placeable component
// where the pointer points: a click places what hovering shows, a drag turns
// it first, a carry from the shelf follows the pointer. docs/editor/placement.md
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
    void OnCancel(ToolContext& ctx) override;
    void OnResourceChanged(ToolContext& ctx, AssetType type) override;
    InputConsumed OnHover(ToolContext& ctx, EditorViewport& viewport, Vec2d pos) override;
    void OnHoverEnd(ToolContext& ctx) override;
    InputConsumed OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    std::unique_ptr<IInteraction> BeginDrag(ToolContext& ctx, EditorViewport& viewport,
                                            const PointerEvent& pressPointer) override;
    std::unique_ptr<IInteraction> TakeHandedGesture(ToolContext& ctx) override;

    [[nodiscard]] std::vector<PropertyNode> Parameters(const ToolContext& ctx) const override;
    PropertyEditResult SetParameter(ToolContext& ctx, std::string_view key, std::string_view value) override;
    [[nodiscard]] std::string Hint(const ToolContext& ctx) const override;

private:
    void RefreshKinds() const;
    [[nodiscard]] const std::string* ChosenLabel() const;
    // What a placement would put down now; nullopt with no prefab in hand.
    [[nodiscard]] std::optional<PlacementSubject> Subject(const ToolContext& ctx) const;
    // The pending placement takes the subject now in hand, where it stands.
    void Reaim(ToolContext& ctx);

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
    // Radians: where the next placement starts turned, and where a turn leaves it.
    float RestingYaw = 0.0f;
    std::size_t TurnStep = 1; // into the tool's turn steps
};
