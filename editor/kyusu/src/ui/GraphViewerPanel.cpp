#include "GraphViewerPanel.h"

#include "commands/CommandStack.h"
#include "document/WorldDocument.h"
#include "selection/SelectionService.h"
#include "selection/commands/SelectCommand.h"
#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportLayout.h"

#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

struct GraphNodeView
{
    const ZoneHeader* Zone = nullptr;
    ImVec2 Pixel;
    float Depth = 0.0f;
};

struct GraphEdgeView
{
    EntityId Entity;
    ZoneId A;
    ZoneId B;
    bool Dock = false;
    bool OneWay = false;
    bool CrossGraph = false;
    uint32_t Directions = DockDirectionBoth;
    uint64_t StableId = 0;
};

float DistanceToSegment(ImVec2 point, ImVec2 a, ImVec2 b)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1e-6f)
        return std::hypot(point.x - a.x, point.y - a.y);
    const float t = std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy)
                               / lengthSquared, 0.0f, 1.0f);
    return std::hypot(point.x - (a.x + dx * t), point.y - (a.y + dy * t));
}

const ZoneHeader* FindZone(const WorldPartitionManifest& manifest, ZoneId id)
{
    for (const ZoneHeader& zone : manifest.Zones)
        if (zone.Id == id)
            return &zone;
    return nullptr;
}

bool NameMatches(const ZoneHeader& zone, std::string_view search)
{
    if (search.empty())
        return true;
    std::string name = zone.Name;
    std::string needle(search);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name.find(needle) != std::string::npos;
}

void DrawArrowhead(ImDrawList& draw, ImVec2 tip, ImVec2 tail, ImU32 color)
{
    const float dx = tip.x - tail.x;
    const float dy = tip.y - tail.y;
    const float length = std::max(1.0f, std::hypot(dx, dy));
    const ImVec2 direction{ dx / length, dy / length };
    const ImVec2 side{ -direction.y, direction.x };
    const ImVec2 base{ tip.x - direction.x * 9.0f,
                       tip.y - direction.y * 9.0f };
    draw.AddTriangleFilled(tip,
        ImVec2{ base.x + side.x * 4.0f, base.y + side.y * 4.0f },
        ImVec2{ base.x - side.x * 4.0f, base.y - side.y * 4.0f }, color);
}

void FrameViewport(ViewportLayout& viewports, Vec3d center, float span)
{
    EditorViewport* viewport = viewports.Active();
    if (viewport == nullptr)
        return;
    span = std::max(span, 1.0f);
    if (viewport->Camera.ActiveMode == EditorCamera::Mode::Orthographic)
    {
        viewport->Camera.OrthoCenter = center;
        viewport->Camera.OrthoHeight = span * 1.5f;
        return;
    }
    viewport->Camera.Position = center
        - viewport->Camera.GetForwardVector() * (span * 1.75f);
}

} // namespace

GraphViewerPanel::GraphViewerPanel(WorldDocument& world, SelectionService& selection,
                                   CommandStack& commands, ViewportLayout& viewports)
    : World(world)
    , Selection(selection)
    , Commands(commands)
    , Viewports(viewports)
{
}

void GraphViewerPanel::OnDraw()
{
    if (!World.IsWorld())
        return;
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    const WorldPartitionManifest& manifest = World.Manifest();
    const auto frameZones = [&](bool selectedOnly)
    {
        Aabb3d bounds = Aabb3d::Empty();
        for (const ZoneHeader& zone : manifest.Zones)
        {
            if (!zone.Bounds.IsValid()
                || (FilterGraph.IsValid() && zone.Graph != FilterGraph)
                || (selectedOnly && zone.Id != World.SelectedZone()))
                continue;
            bounds.ExpandToInclude(zone.Bounds.Min);
            bounds.ExpandToInclude(zone.Bounds.Max);
        }
        if (!bounds.IsValid())
            return;
        Center = bounds.Center();
        const Vec3d extent = bounds.Extent();
        const float span = std::max({ extent.X, extent.Y, extent.Z, 1.0f });
        Zoom = std::clamp(18.0f / span, 0.05f, 20.0f);
    };

    ImGui::Checkbox("Docks", &ShowDocks);
    ImGui::SameLine();
    ImGui::Checkbox("Links", &ShowLinks);
    ImGui::SameLine();
    ImGui::Checkbox("Cross-graph", &CrossGraphOnly);
    ImGui::SameLine();
    ImGui::Checkbox("One-way", &OneWayOnly);
    ImGui::SameLine();
    const GraphRecord* filteredGraph = nullptr;
    for (const GraphRecord& graph : World.Manifest().Graphs)
        if (graph.Id == FilterGraph)
            filteredGraph = &graph;
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##graph_filter",
                          filteredGraph != nullptr ? filteredGraph->Name.c_str() : "All graphs"))
    {
        if (ImGui::Selectable("All graphs", !FilterGraph.IsValid()))
            FilterGraph = {};
        for (const GraphRecord& graph : World.Manifest().Graphs)
            if (ImGui::Selectable(graph.Name.c_str(), graph.Id == FilterGraph))
                FilterGraph = graph.Id;
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##graph_search", "Search zones", Search, sizeof(Search));
    ImGui::SameLine();
    if (ImGui::Button("Frame All"))
        frameZones(false);
    ImGui::SameLine();
    if (ImGui::Button("Frame Selection"))
        frameZones(true);
    ImGui::SameLine();
    if (ImGui::Button("Perspective"))
        PerspectiveProjection = true;
    ImGui::SameLine();
    if (ImGui::Button("Top"))
    {
        Yaw = 0.0f;
        Pitch = -1.45f;
        PerspectiveProjection = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Front"))
    {
        Yaw = 0.0f;
        Pitch = 0.0f;
        PerspectiveProjection = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Side"))
    {
        Yaw = 1.5707963f;
        Pitch = 0.0f;
        PerspectiveProjection = false;
    }
    ImGui::SameLine();
    const char* validation[] = { "All validation", "Warnings + errors", "Errors" };
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("##validation_filter", &ValidationFilter, validation, 3);
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 32.0f || canvasSize.y < 32.0f)
        return;
    ImGui::InvisibleButton("##graph_canvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft
                               | ImGuiButtonFlags_MouseButtonRight
                               | ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 canvasMax{ canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y };
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvasMin, canvasMax, ImGui::GetColorU32(EditorUi::FrameBg));
    draw->PushClipRect(canvasMin, canvasMax, true);

    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        Yaw += io.MouseDelta.x * 0.008f;
        Pitch = std::clamp(Pitch + io.MouseDelta.y * 0.008f, -1.45f, 1.45f);
        PerspectiveProjection = true;
    }
    if (hovered && io.MouseWheel != 0.0f)
        Zoom = std::clamp(Zoom * std::pow(1.12f, io.MouseWheel), 0.05f, 20.0f);

    if (!manifest.Zones.empty())
    {
        Vec3d derivedCenter;
        int count = 0;
        for (const ZoneHeader& zone : manifest.Zones)
            if (zone.Bounds.IsValid())
            {
                derivedCenter += zone.Bounds.Center();
                ++count;
            }
        if (count > 0 && Center.SqrMagnitude() == 0.0f)
            Center = derivedCenter / static_cast<float>(count);
    }

    const float cy = std::cos(Yaw);
    const float sy = std::sin(Yaw);
    const float cp = std::cos(Pitch);
    const float sp = std::sin(Pitch);
    const float scale = std::min(canvasSize.x, canvasSize.y) * 0.035f * Zoom;
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        const Vec3d right{ cy, 0.0f, -sy };
        const Vec3d up{ -sp * sy, cp, -sp * cy };
        Center -= right * (io.MouseDelta.x / std::max(scale, 1e-4f));
        Center += up * (io.MouseDelta.y / std::max(scale, 1e-4f));
    }
    const auto project = [&](Vec3d point)
    {
        const Vec3d p = point - Center;
        const float x0 = cy * p.X - sy * p.Z;
        const float z0 = sy * p.X + cy * p.Z;
        const float y1 = cp * p.Y - sp * z0;
        const float z1 = sp * p.Y + cp * z0;
        const float perspective = PerspectiveProjection
            ? 1.0f / std::max(0.25f, 1.0f + z1 * 0.015f) : 1.0f;
        return std::pair{ ImVec2{ canvasMin.x + canvasSize.x * 0.5f + x0 * scale * perspective,
                                 canvasMin.y + canvasSize.y * 0.5f - y1 * scale * perspective },
                          z1 };
    };

    const auto severityFor = [&](ContentRiskSourceKind kind, uint64_t source)
    {
        int severity = 0;
        for (const ContentRiskRecord& record : World.ValidationRecords())
        {
            if (record.Kind != kind || record.SourceId != source)
                continue;
            severity = std::max(severity,
                record.Severity == ContentRiskSeverity::Error ? 2 : 1);
        }
        return severity;
    };
    const auto passesValidation = [&](int severity)
    {
        return ValidationFilter == 0 || (ValidationFilter == 1 && severity >= 1)
            || (ValidationFilter == 2 && severity >= 2);
    };

    std::vector<GraphNodeView> nodes;
    nodes.reserve(manifest.Zones.size());
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (!zone.Bounds.IsValid() || !NameMatches(zone, Search)
            || (FilterGraph.IsValid() && zone.Graph != FilterGraph))
            continue;
        const auto [pixel, depth] = project(zone.Bounds.Center());
        nodes.push_back({ &zone, pixel, depth });
    }
    const auto nodeFor = [&](ZoneId id) -> const GraphNodeView*
    {
        for (const GraphNodeView& node : nodes)
            if (node.Zone->Id == id)
                return &node;
        return nullptr;
    };

    std::vector<GraphEdgeView> edges;
    const EditorScene& scene = World.WorldSceneDocument().GetScene();
    const Registry& registry = World.WorldSceneDocument().GetRegistry();
    for (EntityId entity : scene.GetAllEntities())
    {
        if (ShowDocks)
            if (const WorldDock* dock = registry.Components.TryGet<WorldDock>(entity))
            {
                const ZoneHeader* a = FindZone(manifest, dock->ZoneA);
                const ZoneHeader* b = FindZone(manifest, dock->ZoneB);
                if (a != nullptr && b != nullptr)
                    edges.push_back({ entity, dock->ZoneA, dock->ZoneB, true,
                                      dock->Directions != DockDirectionBoth,
                                      a->Graph != b->Graph, dock->Directions,
                                      dock->Id.Value });
            }
        if (ShowLinks)
            if (const WorldLink* link = registry.Components.TryGet<WorldLink>(entity))
            {
                const ZoneHeader* a = FindZone(manifest, link->ZoneA);
                const ZoneHeader* b = FindZone(manifest, link->ZoneB);
                if (a != nullptr && b != nullptr)
                    edges.push_back({ entity, link->ZoneA, link->ZoneB, false,
                                      link->Directions != DockDirectionBoth,
                                      a->Graph != b->Graph, link->Directions,
                                      link->Id.Value });
            }
    }
    std::sort(edges.begin(), edges.end(), [](const GraphEdgeView& a, const GraphEdgeView& b)
    {
        if (a.A.Value != b.A.Value)
            return a.A.Value < b.A.Value;
        if (a.B.Value != b.B.Value)
            return a.B.Value < b.B.Value;
        return a.StableId < b.StableId;
    });

    const SelectableRef selected = Selection.GetPrimarySelection();
    EntityId clickedEdge;
    float clickedEdgeDistance = 8.0f;
    const ImVec2 mouse = io.MousePos;
    for (std::size_t i = 0; i < edges.size(); ++i)
    {
        const GraphEdgeView& edge = edges[i];
        if ((CrossGraphOnly && !edge.CrossGraph) || (OneWayOnly && !edge.OneWay))
            continue;
        const ContentRiskSourceKind riskKind = edge.Dock
            ? ContentRiskSourceKind::Dock : ContentRiskSourceKind::Link;
        const int edgeSeverity = severityFor(riskKind, edge.StableId);
        if (!passesValidation(edgeSeverity))
            continue;
        const GraphNodeView* a = nodeFor(edge.A);
        const GraphNodeView* b = nodeFor(edge.B);
        if (a == nullptr || b == nullptr)
            continue;
        const float dx = b->Pixel.x - a->Pixel.x;
        const float dy = b->Pixel.y - a->Pixel.y;
        const float length = std::max(1.0f, std::hypot(dx, dy));
        const ZoneId low = edge.A.Value < edge.B.Value ? edge.A : edge.B;
        const ZoneId high = edge.A.Value < edge.B.Value ? edge.B : edge.A;
        std::size_t parallelCount = 0;
        std::size_t parallelOrdinal = 0;
        for (std::size_t candidateIndex = 0; candidateIndex < edges.size(); ++candidateIndex)
        {
            const GraphEdgeView& candidate = edges[candidateIndex];
            const ZoneId candidateLow = candidate.A.Value < candidate.B.Value
                ? candidate.A : candidate.B;
            const ZoneId candidateHigh = candidate.A.Value < candidate.B.Value
                ? candidate.B : candidate.A;
            if (candidateLow != low || candidateHigh != high)
                continue;
            if (candidateIndex < i)
                ++parallelOrdinal;
            ++parallelCount;
        }
        const float offsetIndex = static_cast<float>(parallelOrdinal)
            - static_cast<float>(parallelCount - 1) * 0.5f;
        const ImVec2 offset{ -dy / length * offsetIndex * 3.0f,
                             dx / length * offsetIndex * 3.0f };
        const ImVec2 p0{ a->Pixel.x + offset.x, a->Pixel.y + offset.y };
        const ImVec2 p1{ b->Pixel.x + offset.x, b->Pixel.y + offset.y };
        const bool isSelected = selected.Entity == edge.Entity
            && selected.Registry == registry.Id;
        ImU32 color = edge.Dock ? ImGui::GetColorU32(EditorUi::Accent)
                                : ImGui::GetColorU32(EditorUi::SecondaryHover);
        if (edge.CrossGraph)
            color = ImGui::GetColorU32(EditorUi::Warning);
        if (edgeSeverity == 1)
            color = ImGui::GetColorU32(EditorUi::Warning);
        else if (edgeSeverity >= 2)
            color = ImGui::GetColorU32(EditorUi::Danger);
        if (isSelected)
            color = ImGui::GetColorU32(EditorUi::AccentHover);
        if (edge.Dock)
            draw->AddLine(p0, p1, color, isSelected ? 3.5f : 2.0f);
        else
        {
            const int segments = 10;
            for (int segment = 0; segment < segments; segment += 2)
            {
                const float t0 = static_cast<float>(segment) / segments;
                const float t1 = static_cast<float>(segment + 1) / segments;
                draw->AddLine(ImVec2{ p0.x + dx * t0, p0.y + dy * t0 },
                              ImVec2{ p0.x + dx * t1, p0.y + dy * t1 }, color, 2.0f);
            }
        }
        if ((edge.Directions & DockDirectionAToB) != 0)
            DrawArrowhead(*draw, p1, p0, color);
        if ((edge.Directions & DockDirectionBToA) != 0)
            DrawArrowhead(*draw, p0, p1, color);
        const float distance = DistanceToSegment(mouse, p0, p1);
        if (distance < clickedEdgeDistance)
        {
            clickedEdgeDistance = distance;
            clickedEdge = edge.Entity;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const GraphNodeView& a, const GraphNodeView& b)
              { return a.Depth > b.Depth; });
    ZoneId clickedZone;
    float clickedNodeDistance = 14.0f;
    for (const GraphNodeView& node : nodes)
    {
        const bool nodeSelected = World.SelectedZone() == node.Zone->Id;
        const bool focused = World.FocusZone() == node.Zone->Id;
        const int nodeSeverity = severityFor(ContentRiskSourceKind::Zone,
                                             node.Zone->Id.Value);
        ImU32 nodeColor = ImGui::GetColorU32(EditorUi::Accent);
        if (nodeSeverity == 1)
            nodeColor = ImGui::GetColorU32(EditorUi::Warning);
        else if (nodeSeverity >= 2)
            nodeColor = ImGui::GetColorU32(EditorUi::Danger);
        draw->AddCircleFilled(node.Pixel, focused || nodeSelected ? 9.0f : 7.0f,
                              nodeSelected ? ImGui::GetColorU32(EditorUi::Warning)
                              : focused ? ImGui::GetColorU32(EditorUi::AccentHover)
                                        : nodeColor, 18);
        draw->AddText(ImVec2{ node.Pixel.x + 10.0f, node.Pixel.y - 8.0f },
                      ImGui::GetColorU32(EditorUi::TextPrimary), node.Zone->Name.c_str());
        const float distance = std::hypot(mouse.x - node.Pixel.x, mouse.y - node.Pixel.y);
        if (distance < clickedNodeDistance)
        {
            clickedNodeDistance = distance;
            clickedZone = node.Zone->Id;
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (clickedEdge.IsValid() && clickedEdgeDistance < clickedNodeDistance)
        {
            (void)World.SelectZone(ZoneId{});
            World.FocusWorldScene();
            Commands.Execute(std::make_unique<SelectCommand>(
                Selection, SelectableRef::EntitySelection(registry.Id, clickedEdge)));
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (const WorldDock* dock = registry.Components.TryGet<WorldDock>(clickedEdge))
                    if (const LocalTransform* transform =
                            registry.Components.TryGet<LocalTransform>(clickedEdge))
                        FrameViewport(Viewports, transform->Value.Position,
                                      std::max(dock->HalfExtents.X,
                                               dock->HalfExtents.Y) * 2.0f);
            }
        }
        else if (clickedZone.IsValid())
        {
            Selection.ClearSelection();
            (void)World.SelectZone(clickedZone);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (const ZoneHeader* zone = FindZone(manifest, clickedZone))
                    FrameViewport(Viewports, zone->Bounds.Center(),
                                  std::max({ zone->Bounds.Extent().X,
                                             zone->Bounds.Extent().Y,
                                             zone->Bounds.Extent().Z }));
                (void)World.SetFocusZone(clickedZone);
            }
        }
    }
    draw->PopClipRect();
}
