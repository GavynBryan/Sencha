#include "SelectTool.h"

#include "../../commands/CommandStack.h"
#include "../../meshedit/MeshEditService.h"
#include "../../selection/commands/SelectCommand.h"
#include "../../selection/SelectionService.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/Picking.h"

#include <imgui.h>

#include <algorithm>
#include <memory>

namespace
{
// Maps the active mesh-element mode onto what a click selects. Object mode picks
// the entity body; element modes pick that element kind only.
BrushPickMode PickModeFor(MeshElementKind kind)
{
    switch (kind)
    {
    case MeshElementKind::Vertex: return BrushPickMode::VertexOnly;
    case MeshElementKind::Edge:   return BrushPickMode::EdgeOnly;
    case MeshElementKind::Face:   return BrushPickMode::FaceOnly;
    case MeshElementKind::Object:
    default:                      return BrushPickMode::EntityOnly;
    }
}
}

std::string_view SelectTool::GetId() const
{
    return "select";
}

std::string_view SelectTool::GetDisplayName() const
{
    return "Select";
}

InputConsumed SelectTool::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 point)
{
    const SelectableRef picked = ctx.Picking.Pick(
        viewport,
        point,
        ctx.Scene,
        BrushPickRequest{ .Mode = PickModeFor(ctx.MeshEdit.GetElementKind()) });

    // Shift/Ctrl extend the current set; a plain click replaces it. ImGui's IO
    // modifier state is current here: SDL events feed ImGui before tool routing.
    const ImGuiIO& io = ImGui::GetIO();
    const bool additive = io.KeyShift || io.KeyCtrl;

    if (!additive)
    {
        ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, picked));
        return InputConsumed::Yes;
    }

    // An additive click on empty space is a no-op: it must not clear the set.
    if (!picked.IsValid())
        return InputConsumed::Yes;

    SelectionSnapshot snapshot = ctx.Selection.GetSnapshot();
    const auto it = std::find(snapshot.Items.begin(), snapshot.Items.end(), picked);
    if (it != snapshot.Items.end())
    {
        snapshot.Items.erase(it);
        if (snapshot.Primary == picked)
            snapshot.Primary = snapshot.Items.empty() ? SelectableRef{} : snapshot.Items.back();
    }
    else
    {
        snapshot.Items.push_back(picked);
        snapshot.Primary = picked;
    }

    ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, std::move(snapshot)));
    return InputConsumed::Yes;
}
