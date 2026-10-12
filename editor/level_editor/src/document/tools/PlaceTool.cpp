#include "document/tools/PlaceTool.h"

#include "authoring/PlaceIcons.h"
#include "icons/IconNames.h"

#include "document/DocumentSerialization.h"
#include "document/interactions/PlaceCarryInteraction.h"
#include "document/interactions/PlaceTurnInteraction.h"
#include "document/placement/PlacementCommand.h"
#include "document/placement/PlacementResolver.h"
#include "document/placement/PlacementSession.h"
#include "properties/SettingProperties.h"
#include "resources/ActiveResources.h"
#include "tools/ToolContext.h"

#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace
{
constexpr std::array<std::string_view, 3> kTurnSteps = { "15", "45", "90" };
constexpr std::array<double, 3> kTurnStepDegrees = { 15.0, 45.0, 90.0 };
constexpr ResourceRole kRoles[] = { { .Role = "Prefab", .Type = AssetType::Scene, .Primary = true } };

[[nodiscard]] float Degrees(float radians) { return radians * 180.0f / std::numbers::pi_v<float>; }
[[nodiscard]] float Radians(double degrees) { return static_cast<float>(degrees) * std::numbers::pi_v<float> / 180.0f; }
} // namespace

ITool::Shortcut PlaceTool::GetShortcut() const { return { SDLK_P, {} }; }

std::span<const ResourceRole> PlaceTool::Resources() const
{
    if (Chosen.Source != PlaceSource::Prefab)
        return {};
    return kRoles;
}

void PlaceTool::RefreshKinds() const
{
    std::vector<PlaceKind> kinds;
    for (const auto& serializer : EditorSceneSerializers().Entries())
        if (const ComponentPlacementFacts facts = serializer->Placement(); !facts.Label.empty())
            kinds.push_back({ .Label = std::string(facts.Label),
                              .What = { PlaceSource::Component, std::string(serializer->JsonKey()) },
                              .Icon = PlaceIconOf(*serializer) });
    // Registration order differs between the engine's and a module's
    // components; by label, the wheel reads the same however they arrived.
    std::ranges::sort(kinds, {}, &PlaceKind::Label);
    kinds.insert(kinds.begin(), { PlaceKind{ .Label = "Prefab", .What = {}, .Icon = IconId::Model },
                                  PlaceKind{ .Label = "Empty", .What = { PlaceSource::Empty }, .Icon = IconId::Empty } });
    const auto same = [](const PlaceKind& a, const PlaceKind& b) { return a.What == b.What && a.Icon == b.Icon; };
    if (std::ranges::equal(kinds, Kinds, same))
        return;
    Kinds = std::move(kinds);
    KindChoices.clear();
    for (const PlaceKind& kind : Kinds)
        KindChoices.push_back({ .Label = kind.Label, .Icon = kind.Icon });
}

std::span<const ITool::Variant> PlaceTool::GetVariants() const
{
    RefreshKinds();
    return KindChoices;
}

int PlaceTool::GetActiveVariant(const ToolContext&) const
{
    RefreshKinds();
    const auto found = std::ranges::find(Kinds, Chosen, &PlaceKind::What);
    return found != Kinds.end() ? static_cast<int>(found - Kinds.begin()) : 0;
}

void PlaceTool::SelectVariant(ToolContext& ctx, std::size_t index)
{
    RefreshKinds();
    if (index < Kinds.size())
        Chosen = Kinds[index].What;
    Reaim(ctx);
}

const std::string* PlaceTool::ChosenLabel() const
{
    RefreshKinds();
    const auto found = std::ranges::find(Kinds, Chosen, &PlaceKind::What);
    return found != Kinds.end() ? &found->Label : nullptr;
}

std::optional<PlacementSubject> PlaceTool::Subject(const ToolContext& ctx) const
{
    switch (Chosen.Source)
    {
    case PlaceSource::Prefab:
        if (const AssetRef& prefab = ctx.Resources.Active(AssetType::Scene); prefab.IsValid())
            return PlacementSubject{ .What = PlacementSubject::Kind::SceneSource, .Key = prefab.Path };
        return std::nullopt;
    case PlaceSource::Empty:
        return PlacementSubject{ .What = PlacementSubject::Kind::Entity, .Key = {} };
    case PlaceSource::Component:
        return PlacementSubject{ .What = PlacementSubject::Kind::Entity, .Key = Chosen.ComponentKey };
    }
    return std::nullopt;
}

void PlaceTool::Reaim(ToolContext& ctx)
{
    const PendingPlacement* pending = ctx.Placement.Pending();
    if (pending == nullptr || ctx.Placement.Phase() != PlacementPhase::Aiming)
        return;
    if (const std::optional<PlacementSubject> subject = Subject(ctx))
        ctx.Placement.Aim(*subject, pending->Site, RestingYaw, ctx.Document);
    else
        ctx.Placement.Withdraw();
}

void PlaceTool::OnDeactivate(ToolContext& ctx)
{
    ctx.Placement.Clear();
}

void PlaceTool::OnCancel(ToolContext& ctx)
{
    ctx.Placement.Clear();
}

void PlaceTool::OnResourceChanged(ToolContext& ctx, AssetType type)
{
    if (type == AssetType::Scene)
        Reaim(ctx);
}

InputConsumed PlaceTool::OnHover(ToolContext& ctx, EditorViewport& viewport, Vec2d pos)
{
    const std::optional<PlacementSubject> subject = Subject(ctx);
    const std::optional<PlacementSite> site =
        subject.has_value() ? PlacementSiteAt(ctx.Picking, viewport, pos, ctx.Scene, ctx.Grid) : std::nullopt;
    if (!site.has_value())
    {
        ctx.Placement.Withdraw();
        return InputConsumed::No;
    }
    ctx.Placement.Aim(*subject, *site, RestingYaw, ctx.Document);
    return InputConsumed::Yes;
}

void PlaceTool::OnHoverEnd(ToolContext& ctx)
{
    ctx.Placement.Withdraw();
}

InputConsumed PlaceTool::OnClick(ToolContext& ctx, EditorViewport&, const PointerEvent&)
{
    // The pending placement was settled at the press; the click makes it.
    if (ctx.Placement.Phase() != PlacementPhase::Aiming)
        return InputConsumed::No;
    (void)PlacePending(ctx.Placement, ctx.Document, ctx.Selection, ctx.Commands);
    return InputConsumed::Yes;
}

std::unique_ptr<IInteraction> PlaceTool::BeginDrag(ToolContext& ctx, EditorViewport&, const PointerEvent&)
{
    if (!ctx.Placement.BeginTurn())
        return nullptr;
    return std::make_unique<PlaceTurnInteraction>(Radians(kTurnStepDegrees[TurnStep]), RestingYaw);
}

std::unique_ptr<IInteraction> PlaceTool::TakeHandedGesture(ToolContext& ctx)
{
    // What arrives carried is a prefab, now the one in hand.
    Chosen = PlaceChoice{ .Source = PlaceSource::Prefab };
    const std::optional<PlacementSubject> subject = Subject(ctx);
    if (!subject.has_value())
        return nullptr;
    return std::make_unique<PlaceCarryInteraction>(*subject, RestingYaw);
}

std::vector<PropertyNode> PlaceTool::Parameters(const ToolContext&) const
{
    return { NumberProperty("yaw", "Yaw", std::round(Degrees(RestingYaw)), 0.0, 359.0, 1.0,
                            "Degrees the next placement turns about its up axis; dragging from the spot turns it"),
             ChoiceProperty("turn_step", "Turn step", kTurnSteps, TurnStep,
                            "Degrees a drag turns by between stops while snapping is on") };
}

PropertyEditResult PlaceTool::SetParameter(ToolContext& ctx, std::string_view key, std::string_view value)
{
    if (key == "yaw")
    {
        const std::optional<double> degrees = ParseNumber(value, 0.0, 359.0);
        if (!degrees.has_value())
            return PropertyEditResult::Refused;
        RestingYaw = NormalizedYaw(Radians(*degrees));
        Reaim(ctx);
        return PropertyEditResult::Applied;
    }
    if (key == "turn_step")
    {
        const std::optional<std::size_t> choice = ParseChoice(value, kTurnSteps);
        if (!choice.has_value())
            return PropertyEditResult::Refused;
        TurnStep = *choice;
        return PropertyEditResult::Applied;
    }
    return PropertyEditResult::Gone;
}

std::string PlaceTool::Hint(const ToolContext& ctx) const
{
    const PendingPlacement* pending = ctx.Placement.Pending();
    if (pending != nullptr && !pending->Refusal.empty())
        return "Can't place this: " + pending->Refusal;
    if (ctx.Placement.Phase() == PlacementPhase::Turning)
        return ctx.Placement.TurnToward().has_value()
                   ? std::format("Release to place it facing {:.0f}°", Degrees(pending->Yaw))
                   : "Drag away from the spot, in a view that looks onto it, to turn it";
    if (!Subject(ctx).has_value())
        return "Pick a prefab to place";
    if (const std::string* label = ChosenLabel(); label != nullptr && Chosen.Source != PlaceSource::Prefab)
    {
        const bool vowel = std::string_view("AEIOU").find(label->front()) != std::string_view::npos;
        return std::format("Click to place {} {}, or drag to turn it", vowel ? "an" : "a", *label);
    }
    return "Click to place, or drag to turn it";
}
