#include "document/tools/PlaceTool.h"

#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/DocumentSerialization.h"
#include "document/commands/PlaceComponentEntityCommand.h"
#include "document/commands/SceneInstanceCommands.h"
#include "viewport/GridSettings.h"
#include "overlay/EditorOverlayState.h"
#include "properties/SettingProperties.h"
#include "resources/ActiveResources.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/Picking.h"

#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace
{
constexpr std::array<std::string_view, 4> kYaws = { "0", "90", "180", "270" };
constexpr ResourceRole kRoles[] = { { .Role = "Prefab", .Type = AssetType::Scene, .Primary = true } };
constexpr Vec4 kGhost = { 0.35f, 0.85f, 1.0f, 1.0f };
} // namespace

ITool::Shortcut PlaceTool::GetShortcut() const { return { SDLK_P, {} }; }

std::span<const ResourceRole> PlaceTool::Resources() const
{
    if (!Chosen.empty())
        return {};
    return kRoles;
}

void PlaceTool::RefreshKinds() const
{
    std::vector<PlaceKind> kinds;
    for (const auto& serializer : EditorSceneSerializers().Entries())
        if (const std::string_view label = serializer->PlacementLabel(); !label.empty())
            kinds.push_back({ std::string(label), std::string(serializer->JsonKey()) });
    // Registration order differs between the engine's and a module's
    // components; by name, the wheel reads the same however they arrived.
    std::ranges::sort(kinds, {}, &PlaceKind::Label);
    kinds.insert(kinds.begin(), PlaceKind{ "Prefab", {} });
    const auto same = [](const PlaceKind& a, const PlaceKind& b) { return a.ComponentKey == b.ComponentKey; };
    if (std::ranges::equal(kinds, Kinds, same))
        return;
    Kinds = std::move(kinds);
    KindChoices.clear();
    for (const PlaceKind& kind : Kinds)
        KindChoices.push_back({ .Label = kind.Label });
}

std::span<const ITool::Variant> PlaceTool::GetVariants() const
{
    RefreshKinds();
    return KindChoices;
}

int PlaceTool::GetActiveVariant(const ToolContext&) const
{
    RefreshKinds();
    const auto found = std::ranges::find(Kinds, Chosen, &PlaceKind::ComponentKey);
    return found != Kinds.end() ? static_cast<int>(found - Kinds.begin()) : 0;
}

void PlaceTool::SelectVariant(ToolContext&, std::size_t index)
{
    RefreshKinds();
    if (index < Kinds.size())
        Chosen = Kinds[index].ComponentKey;
}

const std::string* PlaceTool::ChosenLabel() const
{
    RefreshKinds();
    const auto found = std::ranges::find(Kinds, Chosen, &PlaceKind::ComponentKey);
    return found != Kinds.end() ? &found->Label : nullptr;
}

std::optional<Vec3d> PlaceTool::Target(ToolContext& ctx, const EditorViewport& viewport, Vec2d pos)
{
    if (const std::optional<SurfaceHit> hit = ctx.Picking.PickSurface(viewport, pos, ctx.Scene))
        return hit->Point;
    return ctx.Picking.ProjectPointToGrid(viewport, pos, ctx.Grid);
}

void PlaceTool::ShowGhost(ToolContext& ctx, Vec3d point)
{
    ClearGhost(ctx);
    // A cross a grid step across on each axis: where the placement's root lands.
    const float reach = std::max(0.25f, ctx.Grid.Spacing * 0.5f);
    for (const Vec3d axis : { Vec3d(1.0f, 0.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), Vec3d(0.0f, 0.0f, 1.0f) })
        ctx.Overlay.Segments.push_back(
            { .From = point - axis * reach, .To = point + axis * reach, .Color = kGhost, .Thickness = 2.0f });
}

void PlaceTool::ClearGhost(ToolContext& ctx)
{
    ctx.Overlay.Segments.clear();
}

void PlaceTool::OnDeactivate(ToolContext& ctx)
{
    ClearGhost(ctx);
}

InputConsumed PlaceTool::OnHover(ToolContext& ctx, EditorViewport& viewport, Vec2d pos)
{
    if (const std::optional<Vec3d> point = Target(ctx, viewport, pos))
        ShowGhost(ctx, *point);
    else
        ClearGhost(ctx);
    return InputConsumed::No;
}

void PlaceTool::OnHoverEnd(ToolContext& ctx)
{
    ClearGhost(ctx);
}

InputConsumed PlaceTool::OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const AssetRef& prefab = ctx.Resources.Active(AssetType::Scene);
    if (Chosen.empty() && !prefab.IsValid())
        return InputConsumed::No;
    const std::optional<Vec3d> point = Target(ctx, viewport, pointer.Position);
    if (!point.has_value())
        return InputConsumed::No;
    Transform3f placement = Transform3f::Identity();
    placement.Position = *point;
    placement.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), static_cast<float>(YawDegrees) * 3.14159265f / 180.0f);
    if (Chosen.empty())
        ctx.Commands.Execute(std::make_unique<PlaceSceneInstanceCommand>(prefab.Path, placement, ctx.Document,
                                                                         ctx.Selection));
    else
        ctx.Commands.Execute(std::make_unique<PlaceComponentEntityCommand>(Chosen, placement, ctx.Document,
                                                                           ctx.Selection));
    return InputConsumed::Yes;
}

std::vector<PropertyNode> PlaceTool::Parameters(const ToolContext&) const
{
    return { ChoiceProperty("yaw", "Yaw", kYaws, static_cast<std::size_t>(YawDegrees / 90),
                            "Degrees the placement turns about the up axis") };
}

PropertyEditResult PlaceTool::SetParameter(ToolContext&, std::string_view key, std::string_view value)
{
    if (key != "yaw")
        return PropertyEditResult::Gone;
    const std::optional<std::size_t> choice = ParseChoice(value, kYaws);
    if (!choice.has_value())
        return PropertyEditResult::Refused;
    YawDegrees = static_cast<int>(*choice) * 90;
    return PropertyEditResult::Applied;
}

std::string PlaceTool::Hint(const ToolContext& ctx) const
{
    if (const std::string* label = ChosenLabel(); label != nullptr && !Chosen.empty())
        return std::format("Click to place a {}", *label);
    return ctx.Resources.Active(AssetType::Scene).IsValid() ? "Click to place" : "Pick a prefab to place";
}
