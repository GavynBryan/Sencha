#include "workspace/ElementVerbs.h"

#include "actions/ActionOffer.h"
#include "properties/SettingProperties.h"
#include "workspace/BrushManipulationSink.h"
#include "workspace/EditorWorkspace.h"

#include <algorithm>
#include <format>

namespace
{
constexpr std::string_view kInset = "inset";
constexpr std::string_view kBevelWidth = "bevel_width";
constexpr std::string_view kBevelSegments = "bevel_segments";
constexpr std::string_view kBridgeSegments = "bridge_segments";
constexpr std::string_view kWeld = "weld";

PropertyNode Distance(std::string_view key, std::string label, float value, std::string tooltip)
{
    // Unbounded: typed, not slid.
    return NumberProperty(std::string(key), std::move(label), value, 0.0, -1.0, 0.05, std::move(tooltip));
}
} // namespace

ElementVerbs::ElementVerbs(EditorWorkspace& workspace, std::function<void()> exportGlb)
    : Workspace(workspace)
    , ExportGlb(std::move(exportGlb))
{
}

std::size_t ElementVerbs::Selected(MeshElementKind kind) const
{
    std::size_t count = 0;
    for (const SelectableRef& ref : Workspace.Selection.GetSelection())
    {
        switch (kind)
        {
        case MeshElementKind::Face:   count += ref.IsFace() ? 1 : 0; break;
        case MeshElementKind::Edge:   count += ref.IsEdge() ? 1 : 0; break;
        case MeshElementKind::Vertex: count += ref.IsVertex() ? 1 : 0; break;
        case MeshElementKind::Object:
        default:
            count += ref.IsEntity() && Workspace.Interaction.Sink != nullptr
                         && Workspace.Interaction.Sink->Resolve(ref.Entity).has_value()
                   ? 1
                   : 0;
            break;
        }
    }
    return count;
}

bool ElementVerbs::HasPending() const
{
    return Workspace.HasPendingInset() || Workspace.HasPendingBevel() || Workspace.HasPendingBridge();
}

void ElementVerbs::Apply(MeshEditVerb verb, const MeshEditParams& params, bool renumbers)
{
    if (Workspace.Interaction.Sink == nullptr)
        return;
    if (auto command = Workspace.MeshEdit.ApplyVerb(*Workspace.Interaction.Sink, Workspace.Selection.GetSnapshot(),
                                                    verb, params))
    {
        Workspace.Commands.Execute(std::move(command));
        if (renumbers)
            Workspace.Selection.ClearMeshElementSelections();
    }
}

void ElementVerbs::Offer(ActionCatalog& catalog, ActionContext& context)
{
    EditorWorkspace& w = Workspace;
    const ActionOffers offer{ catalog, context, "Selection" };
    const auto brushes = [this](std::size_t atLeast) { return [this, atLeast] { return Selected(MeshElementKind::Object) >= atLeast; }; };
    const auto elements = [this](MeshElementKind kind) {
        return [this, kind] { return !HasPending() && Selected(kind) > 0; };
    };

    offer({ .Id = "brush.normals", .Label = "Recalculate Normals" },
          { .Invoke = [this] { Apply(MeshEditVerb::RecalculateNormals, {}, false); }, .Enabled = brushes(1) });
    offer({ .Id = "brush.make_unique", .Label = "Make Unique" },
          { .Invoke = [&w] { w.Actions.MakeUnique(); }, .Enabled = [&w] { return w.Actions.HasInstancedSelection(); } });
    offer({ .Id = "brush.merge", .Label = "Merge Brushes" },
          { .Invoke = [&w] { w.Actions.Merge(); }, .Enabled = brushes(2) });
    offer({ .Id = "brush.bake", .Label = "Bake to Static Mesh" },
          { .Invoke = [&w] { w.Actions.Bake(); }, .Enabled = brushes(1) });
    offer({ .Id = "brush.revert", .Label = "Revert to Brush" },
          { .Invoke = [&w] { w.Actions.RevertBaked(); }, .Enabled = [&w] { return w.Actions.HasBakedSelection(); } });
    offer({ .Id = "brush.export", .Label = "Export .glb..." }, { .Invoke = [this] { if (ExportGlb) ExportGlb(); },
            .Enabled = [&w] { return w.Actions.SelectedExportMesh() != nullptr; } });

    offer({ .Id = "face.flip", .Label = "Flip Normals" },
          { .Invoke = [this] { Apply(MeshEditVerb::FlipFaceNormal, {}, true); }, .Enabled = elements(MeshElementKind::Face) });
    offer({ .Id = "face.separate", .Label = "Separate" },
          { .Invoke = [&w] { w.Actions.SeparateFaces(); }, .Enabled = elements(MeshElementKind::Face) });
    offer({ .Id = "face.inset", .Label = "Inset" },
          { .Invoke = [this] { Workspace.BeginInsetOnSelectedFaces(Current.InsetDistance); },
            .Enabled = elements(MeshElementKind::Face) });
    offer({ .Id = "material.clear", .Label = "Clear Material" },
          { .Invoke = [&w] {
                if (w.Interaction.Sink != nullptr)
                    ApplyMaterialToSelectedFaces(*w.Interaction.Sink, w.Selection, w.Commands, AssetRef{});
            },
            .Enabled = elements(MeshElementKind::Face) });

    const auto soften = [this](bool on) {
        MeshEditParams params;
        params.Soften = on;
        Apply(MeshEditVerb::SetEdgeSoftness, params, false);
    };
    offer({ .Id = "edge.soften", .Label = "Soften" },
          { .Invoke = [soften] { soften(true); }, .Enabled = elements(MeshElementKind::Edge) });
    offer({ .Id = "edge.harden", .Label = "Harden" },
          { .Invoke = [soften] { soften(false); }, .Enabled = elements(MeshElementKind::Edge) });
    offer({ .Id = "edge.bevel", .Label = "Bevel" },
          { .Invoke = [this] { Workspace.BeginBevelOnSelectedEdges(Current.BevelWidth, Current.BevelSegments); },
            .Enabled = elements(MeshElementKind::Edge) });
    offer({ .Id = "edge.bridge", .Label = "Bridge" },
          { .Invoke = [this] { Workspace.Actions.Bridge(Current.BridgeSegments); },
            .Enabled = [this] { return !HasPending() && Selected(MeshElementKind::Edge) >= 2; } });

    offer({ .Id = "vertex.weld", .Label = "Weld by Distance" },
          { .Invoke = [this] {
                MeshEditParams params;
                params.WeldDistance = Current.WeldDistance;
                Apply(MeshEditVerb::WeldVertices, params, true);
            },
            .Enabled = elements(MeshElementKind::Vertex) });
    offer({ .Id = "vertex.snap", .Label = "Snap to Grid" },
          { .Invoke = [this] {
                const GridSettings& grid = Workspace.Grid;
                MeshEditParams params;
                params.GridOrigin = grid.Origin;
                params.GridAxisU = grid.AxisU;
                params.GridAxisV = grid.AxisV;
                params.GridSpacing = grid.Spacing;
                Apply(MeshEditVerb::SnapVerticesToGrid, params, true);
            },
            .Enabled = elements(MeshElementKind::Vertex) });

    offer({ .Id = "element.apply", .Label = "Apply" },
          { .Invoke = [&w] {
                if (w.HasPendingBridge())
                    w.CommitPendingBridge();
                else
                    w.CommitPendingElementEdit();
            },
            .Enabled = [this] { return HasPending(); } });
    offer({ .Id = "element.cancel", .Label = "Cancel" },
          { .Invoke = [&w] {
                if (w.HasPendingBridge())
                    w.CancelPendingBridge();
                else
                    w.CancelPendingElementEdit();
            },
            .Enabled = [this] { return HasPending(); } });
}

std::vector<std::string> ElementVerbs::Relevant() const
{
    if (HasPending())
        return { "element.apply", "element.cancel" };
    switch (Workspace.MeshEdit.GetElementKind())
    {
    case MeshElementKind::Face:
        return { "edit.delete", "face.flip", "face.separate", "face.inset", "material.apply", "material.clear" };
    case MeshElementKind::Edge:
        return { "edge.soften", "edge.harden", "edit.dissolve", "edge.bevel", "edge.bridge" };
    case MeshElementKind::Vertex:
        return { "vertex.weld", "vertex.snap" };
    case MeshElementKind::Object:
    default:
        // A brush's verbs are for brushes and what was baked from them; a light
        // or an empty is only duplicated or deleted.
        if (Selected(MeshElementKind::Object) == 0 && !Workspace.Actions.HasBakedSelection())
            return { "edit.duplicate", "edit.duplicate_instance", "edit.delete" };
        return { "brush.normals", "edit.duplicate", "edit.duplicate_instance", "brush.make_unique", "brush.merge",
                 "brush.bake", "brush.revert", "brush.export", "edit.delete" };
    }
}

std::vector<PropertyNode> ElementVerbs::Nodes() const
{
    switch (Workspace.MeshEdit.GetElementKind())
    {
    case MeshElementKind::Face:
        return { Distance(kInset, "Inset", Current.InsetDistance,
                          "Along each face's normal: out makes a rim, in (negative) a recess") };
    case MeshElementKind::Edge:
        return { Distance(kBevelWidth, "Bevel width", Current.BevelWidth, "How far the bevel retreats into each face"),
                 NumberProperty(std::string(kBevelSegments), "Bevel segments", Current.BevelSegments, 1.0, 16.0, 1.0,
                                "Strips across the profile: one is a flat chamfer, more are rounder"),
                 NumberProperty(std::string(kBridgeSegments), "Bridge segments", Current.BridgeSegments, 1.0, 64.0, 1.0,
                                "Quad rows spanning the gap; more bow the bridge") };
    case MeshElementKind::Vertex:
        return { Distance(kWeld, "Weld distance", Current.WeldDistance, "Vertices closer than this merge") };
    case MeshElementKind::Object:
    default:
        return {};
    }
}

std::string ElementVerbs::ValueOf(std::string_view key) const
{
    for (const PropertyNode& node : Nodes())
        if (node.Key == key)
            return node.Value;
    return {};
}

bool ElementVerbs::BeginEdit(std::string_view key)
{
    EditKey = std::string(key);
    EditBegan = ValueOf(key);
    return !EditBegan.empty();
}

PropertyEditResult ElementVerbs::PreviewEdit(std::string_view key, std::string_view value)
{
    return Set(key, value);
}

PropertyEditResult ElementVerbs::CommitEdit(std::string_view key, std::string_view value)
{
    const PropertyEditResult result = Set(key, value);
    EditKey.clear();
    return result;
}

void ElementVerbs::CancelEdit(std::string_view key)
{
    if (key == EditKey)
        (void)Set(key, EditBegan);
    EditKey.clear();
}

PropertyEditResult ElementVerbs::Set(std::string_view key, std::string_view value)
{
    if (value == ValueOf(key))
        return PropertyEditResult::Unchanged;
    if (key == kInset)
    {
        const std::optional<double> distance = ParseNumber(value);
        if (!distance.has_value())
            return PropertyEditResult::Refused;
        Current.InsetDistance = static_cast<float>(*distance);
        if (Workspace.HasPendingInset())
            Workspace.SetPendingInsetDistance(Current.InsetDistance);
    }
    else if (key == kBevelWidth || key == kBevelSegments)
    {
        const std::optional<double> number = key == kBevelWidth ? ParseNumber(value, 0.0) : ParseNumber(value, 1.0, 16.0);
        if (!number.has_value())
            return PropertyEditResult::Refused;
        if (key == kBevelWidth)
            Current.BevelWidth = static_cast<float>(*number);
        else
            Current.BevelSegments = static_cast<int>(std::lround(*number));
        if (Workspace.HasPendingBevel())
            Workspace.SetPendingBevelParams(Current.BevelWidth, Current.BevelSegments);
    }
    else if (key == kBridgeSegments)
    {
        const std::optional<double> segments = ParseNumber(value, 1.0, 64.0);
        if (!segments.has_value())
            return PropertyEditResult::Refused;
        Current.BridgeSegments = static_cast<int>(std::lround(*segments));
        if (Workspace.HasPendingBridge())
            Workspace.SetPendingBridgeSegments(Current.BridgeSegments);
    }
    else if (key == kWeld)
    {
        const std::optional<double> distance = ParseNumber(value, 0.0);
        if (!distance.has_value())
            return PropertyEditResult::Refused;
        Current.WeldDistance = static_cast<float>(*distance);
    }
    else
    {
        return PropertyEditResult::Gone;
    }
    return PropertyEditResult::Applied;
}
