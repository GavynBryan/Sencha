#include "WorldDockEditorAdapter.h"

#include "EditorTheme.h"
#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDockAuthoring.h"
#include "document/WorldDocument.h"
#include "document/commands/ValueCommand.h"
#include "selection/SelectionService.h"

#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace
{
template <typename Value>
class DockValueTransaction final : public IValueEditTransaction<Value>
{
public:
    using ApplyFn = std::function<void(WorldDock&, const Value&)>;

    DockValueTransaction(EditorComponentContext context, WorldDock before,
                         ApplyFn apply)
        : Context(context), Before(std::move(before)), Apply(std::move(apply)) {}

    void Preview(const Value& value) override
    {
        WorldDock next = Before;
        Apply(next, value);
        Context.Scene.SetComponent(Context.Entity, next);
    }

    void Commit(const Value& value) override
    {
        WorldDock after = Before;
        Apply(after, value);
        Context.Scene.SetComponent(Context.Entity, Before);
        Context.World.Revalidate();
        auto apply = [&scene = Context.Scene, &world = Context.World,
                      entity = Context.Entity](const WorldDock& dock)
        {
            scene.SetComponent(entity, dock);
            world.Revalidate();
        };
        Context.Commands.Execute(std::make_unique<ValueCommand<WorldDock>>(
            Before, after, std::move(apply), Context.Document));
    }

    void Cancel() override
    {
        Context.Scene.SetComponent(Context.Entity, Before);
        Context.World.Revalidate();
    }

private:
    EditorComponentContext Context;
    WorldDock Before;
    ApplyFn Apply;
};

void AppendLine(ViewportAffordanceOutput& output, Vec3d a, Vec3d b,
                Vec4 color, float width = EditorTheme::OverlayLinePixels)
{
    output.Lines.push_back({ a, b, color, width });
}

void AppendQuad(ViewportAffordanceOutput& output, Vec3d a, Vec3d b,
                Vec3d c, Vec3d d, Vec4 color)
{
    output.FillTriangles.push_back({ a, color });
    output.FillTriangles.push_back({ b, color });
    output.FillTriangles.push_back({ c, color });
    output.FillTriangles.push_back({ a, color });
    output.FillTriangles.push_back({ c, color });
    output.FillTriangles.push_back({ d, color });
}

void AppendArrow(ViewportAffordanceOutput& output, Vec3d from, Vec3d to,
                 Vec3d wingAxis, Vec4 color)
{
    AppendLine(output, from, to, color, 2.0f);
    Vec3d direction = to - from;
    if (direction.SqrMagnitude() <= 1.0e-8f)
        return;
    direction = direction.Normalized();
    const Vec3d base = to - direction * 0.28f;
    const Vec3d wing = wingAxis.Normalized() * 0.16f;
    AppendLine(output, to, base + wing, color, 2.0f);
    AppendLine(output, to, base - wing, color, 2.0f);
}

std::array<Vec3d, 8> BoxCorners(const Aabb3d& bounds,
                                const Transform3f& transform)
{
    const Vec3d& lo = bounds.Min;
    const Vec3d& hi = bounds.Max;
    std::array<Vec3d, 8> corners = {
        Vec3d{ lo.X, lo.Y, lo.Z }, Vec3d{ hi.X, lo.Y, lo.Z },
        Vec3d{ hi.X, hi.Y, lo.Z }, Vec3d{ lo.X, hi.Y, lo.Z },
        Vec3d{ lo.X, lo.Y, hi.Z }, Vec3d{ hi.X, lo.Y, hi.Z },
        Vec3d{ hi.X, hi.Y, hi.Z }, Vec3d{ lo.X, hi.Y, hi.Z },
    };
    for (Vec3d& corner : corners)
        corner = transform.TransformPoint(corner);
    return corners;
}

void AppendBox(ViewportAffordanceOutput& output, const Aabb3d& bounds,
               const Transform3f& transform, Vec4 lineColor, Vec4 fillColor)
{
    const auto c = BoxCorners(bounds, transform);
    static constexpr int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7},
    };
    for (const auto& edge : edges)
        AppendLine(output, c[edge[0]], c[edge[1]], lineColor);
    AppendQuad(output, c[0], c[1], c[2], c[3], fillColor);
    AppendQuad(output, c[4], c[7], c[6], c[5], fillColor);
    AppendQuad(output, c[0], c[4], c[5], c[1], fillColor);
    AppendQuad(output, c[3], c[2], c[6], c[7], fillColor);
    AppendQuad(output, c[0], c[3], c[7], c[4], fillColor);
    AppendQuad(output, c[1], c[5], c[6], c[2], fillColor);
}

std::string ZoneName(const WorldPartitionManifest& manifest, ZoneId id)
{
    for (const ZoneHeader& zone : manifest.Zones)
        if (zone.Id == id)
            return zone.Name.empty() ? ZoneIdToString(id) : zone.Name;
    return "unresolved";
}

ZoneId UniqueZoneAt(const WorldPartitionManifest& manifest, Vec3d point,
                    ZoneId exclude = {})
{
    ZoneId result;
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Id == exclude || !zone.Bounds.Contains(point))
            continue;
        if (result.IsValid())
            return {};
        result = zone.Id;
    }
    return result;
}

struct DockEntityState
{
    WorldDock Dock;
    Transform3f Transform;
};

class WorldDockEditorAdapter final : public IEditorComponentAdapter
{
public:
    ComponentTypeId Type() const override
    {
        return ResolveComponentTypeId<WorldDock>();
    }

    bool AllowEntityScale() const override { return false; }

    void BuildViewport(const EditorComponentContext& context,
                       ViewportAffordanceOutput& output) const override
    {
        const Registry& registry = context.Scene.GetRegistry();
        const WorldDock* dock = registry.Components.TryGet<WorldDock>(context.Entity);
        if (dock == nullptr)
            return;

        bool invalid = false;
        for (const ContentRiskRecord& record : context.World.ValidationRecords())
            if (record.Kind == ContentRiskSourceKind::Dock
                && record.SourceId == dock->Id.Value
                && record.Severity == ContentRiskSeverity::Error)
                invalid = true;

        const Vec4 line = invalid ? EditorTheme::PreviewUndemanded
            : context.Selected ? EditorTheme::Selection : EditorTheme::TransitionLine;
        const Vec4 fill{ line.X, line.Y, line.Z, context.Selected ? 0.20f : 0.10f };
        const Vec3d origin = context.Transform.Position;
        const Vec3d right = context.Transform.Right();
        const Vec3d up = context.Transform.Up();
        const Vec3d normal = -context.Transform.Forward();
        const Vec3d r = right * dock->HalfExtents.X;
        const Vec3d u = up * dock->HalfExtents.Y;
        const Vec3d plane[4] = { origin - r - u, origin + r - u,
                                 origin + r + u, origin - r + u };
        for (int i = 0; i < 4; ++i)
            AppendLine(output, plane[i], plane[(i + 1) % 4], line, 2.0f);
        AppendQuad(output, plane[0], plane[1], plane[2], plane[3], fill);

        AppendArrow(output, origin - normal * 0.12f, origin - normal * 1.4f,
                    right, line);
        AppendArrow(output, origin + normal * 0.12f, origin + normal * 1.4f,
                    right, line);
        if ((dock->Directions & DockDirectionAToB) != 0)
            AppendArrow(output, origin - normal * 0.65f + right * 0.22f,
                        origin + normal * 0.65f + right * 0.22f, up, line);
        if ((dock->Directions & DockDirectionBToA) != 0)
            AppendArrow(output, origin + normal * 0.65f - right * 0.22f,
                        origin - normal * 0.65f - right * 0.22f, up, line);

        output.Labels.push_back({ origin - normal * 1.55f, line,
            "A: " + ZoneName(context.World.Manifest(), dock->ZoneA) });
        output.Labels.push_back({ origin + normal * 1.55f, line,
            "B: " + ZoneName(context.World.Manifest(), dock->ZoneB) });
        if (invalid)
            output.Labels.push_back({ origin + up * (dock->HalfExtents.Y + 0.35f), line,
                                      "Invalid Dock" });

        const Vec4 boxFill{ line.X, line.Y, line.Z, 0.08f };
        AppendBox(output, dock->SideAArmBounds, context.Transform, line, boxFill);
        AppendBox(output, dock->SideBArmBounds, context.Transform, line, boxFill);
        output.PickProxies.push_back({
            .Shape = EditorPickProxy::Kind::Rectangle,
            .Entity = context.Entity,
            .LocalToWorld = context.Transform,
            .HalfExtents = dock->HalfExtents,
        });
        output.PickProxies.push_back({
            .Shape = EditorPickProxy::Kind::Box,
            .Entity = context.Entity,
            .LocalToWorld = context.Transform,
            .Bounds = dock->SideAArmBounds,
        });
        output.PickProxies.push_back({
            .Shape = EditorPickProxy::Kind::Box,
            .Entity = context.Entity,
            .LocalToWorld = context.Transform,
            .Bounds = dock->SideBArmBounds,
        });

        if (!context.Selected)
            return;
        const WorldDock before = *dock;
        output.Rectangles.push_back(RectEditTarget{
            .Key = (dock->Id.Value << 2) | 1u,
            .LocalToWorld = context.Transform,
            .HalfExtents = dock->HalfExtents,
            .BeginEdit = [context, before]
            {
                return std::make_unique<DockValueTransaction<Vec2d>>(
                    context, before, [](WorldDock& value, const Vec2d& next)
                    { value.HalfExtents = next; });
            },
        });
        AabbEditTarget sideA{
            .Key = (dock->Id.Value << 2) | 2u,
            .LocalToWorld = context.Transform,
            .Value = dock->SideAArmBounds,
            .BeginEdit = [context, before]
            {
                return std::make_unique<DockValueTransaction<Aabb3d>>(
                    context, before, [](WorldDock& value, const Aabb3d& next)
                    { value.SideAArmBounds = next; });
            },
        };
        sideA.MaximumLimits[2] = 0.0f;
        output.Aabbs.push_back(std::move(sideA));
        AabbEditTarget sideB{
            .Key = (dock->Id.Value << 2) | 3u,
            .LocalToWorld = context.Transform,
            .Value = dock->SideBArmBounds,
            .BeginEdit = [context, before]
            {
                return std::make_unique<DockValueTransaction<Aabb3d>>(
                    context, before, [](WorldDock& value, const Aabb3d& next)
                    { value.SideBArmBounds = next; });
            },
        };
        sideB.MinimumLimits[2] = 0.0f;
        output.Aabbs.push_back(std::move(sideB));
    }

    bool DrawInspector(EditorComponentInspectorContext& context) const override
    {
        Registry& registry = context.Scene.GetRegistry();
        WorldDock* dock = registry.Components.TryGet<WorldDock>(context.Entity);
        LocalTransform* local = registry.Components.TryGet<LocalTransform>(context.Entity);
        if (dock == nullptr || local == nullptr)
            return false;

        const auto applyDock = [&](WorldDock before, WorldDock after)
        {
            context.Commands.Execute(std::make_unique<ValueCommand<WorldDock>>(
                before, after,
                [&scene = context.Scene, &world = context.World,
                 entity = context.Entity](const WorldDock& value)
                {
                    scene.SetComponent(entity, value);
                    world.Revalidate();
                }, context.Document));
        };

        ImGui::Text("Id  %016llx", static_cast<unsigned long long>(dock->Id.Value));
        ImGui::SameLine();
        if (ImGui::SmallButton("Generate##dock_id"))
        {
            WorldDock after = *dock;
            after.Id = context.World.MintDockId();
            applyDock(*dock, after);
        }

        const auto zoneCombo = [&](const char* label, ZoneId current, auto set)
        {
            const std::string currentName = ZoneName(context.World.Manifest(), current);
            if (ImGui::BeginCombo(label, currentName.c_str()))
            {
                for (const ZoneHeader& zone : context.World.Manifest().Zones)
                    if (ImGui::Selectable(zone.Name.c_str(), zone.Id == current))
                        set(zone.Id);
                ImGui::EndCombo();
            }
        };
        zoneCombo("Zone A", dock->ZoneA, [&](ZoneId id)
        { WorldDock after = *dock; after.ZoneA = id; applyDock(*dock, after); });
        zoneCombo("Zone B", dock->ZoneB, [&](ZoneId id)
        { WorldDock after = *dock; after.ZoneB = id; applyDock(*dock, after); });

        int direction = dock->Directions == DockDirectionBoth ? 0
            : dock->Directions == DockDirectionAToB ? 1 : 2;
        const char* directionNames[] = { "Both", "A to B", "B to A" };
        if (ImGui::Combo("Directions", &direction, directionNames, 3))
        {
            WorldDock after = *dock;
            after.Directions = direction == 0 ? DockDirectionBoth
                : direction == 1 ? DockDirectionAToB : DockDirectionBToA;
            applyDock(*dock, after);
        }
        float dimensions[2]{ dock->HalfExtents.X * 2.0f,
                             dock->HalfExtents.Y * 2.0f };
        if (ImGui::DragFloat2("Width / Height", dimensions, 0.05f, 0.01f))
        {
            WorldDock after = *dock;
            after.HalfExtents = { std::max(0.025f, dimensions[0] * 0.5f),
                                  std::max(0.025f, dimensions[1] * 0.5f) };
            applyDock(*dock, after);
        }
        int priority = dock->PreloadPriority;
        if (ImGui::InputInt("Priority", &priority))
        { WorldDock after = *dock; after.PreloadPriority = priority; applyDock(*dock, after); }
        int depth = dock->PreloadDepth;
        if (ImGui::InputInt("Preload depth", &depth))
        { WorldDock after = *dock; after.PreloadDepth = std::max(0, depth); applyDock(*dock, after); }

        char condition[129]{};
        const std::string_view current = dock->DemandCondition.View();
        std::snprintf(condition, sizeof(condition), "%.*s",
                      static_cast<int>(current.size()), current.data());
        if (ImGui::InputText("Demand condition", condition, sizeof(condition)))
        {
            WorldDock after = *dock;
            after.DemandCondition.Assign(condition);
            applyDock(*dock, after);
        }

        const auto drawBounds = [&](const char* label, Aabb3d value, auto member)
        {
            if (!ImGui::TreeNode(label))
                return;
            float minimum[3]{ value.Min.X, value.Min.Y, value.Min.Z };
            float maximum[3]{ value.Max.X, value.Max.Y, value.Max.Z };
            bool changed = ImGui::DragFloat3("Min", minimum, 0.05f);
            changed |= ImGui::DragFloat3("Max", maximum, 0.05f);
            if (changed)
            {
                WorldDock after = *dock;
                after.*member = Aabb3d{ { minimum[0], minimum[1], minimum[2] },
                                        { maximum[0], maximum[1], maximum[2] } };
                applyDock(*dock, after);
            }
            ImGui::TreePop();
        };
        drawBounds("Side A local AABB", dock->SideAArmBounds,
                   &WorldDock::SideAArmBounds);
        drawBounds("Side B local AABB", dock->SideBArmBounds,
                   &WorldDock::SideBArmBounds);

        if (ImGui::Button("Swap Sides"))
        {
            DockEntityState before{ *dock, local->Value };
            DockEntityState after = before;
            SwapWorldDockSides(after.Dock, after.Transform);
            context.Commands.Execute(std::make_unique<ValueCommand<DockEntityState>>(
                before, after,
                [&scene = context.Scene, &world = context.World,
                 entity = context.Entity](const DockEntityState& value)
                {
                    scene.SetComponent(entity, value.Dock);
                    scene.SetTransform(entity, value.Transform);
                    world.Revalidate();
                }, context.Document));
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Arms"))
        {
            WorldDock after = *dock;
            after.SideAArmBounds = { { -after.HalfExtents.X, -after.HalfExtents.Y, -2 },
                                     { after.HalfExtents.X, after.HalfExtents.Y, 0 } };
            after.SideBArmBounds = { { -after.HalfExtents.X, -after.HalfExtents.Y, 0 },
                                     { after.HalfExtents.X, after.HalfExtents.Y, 2 } };
            applyDock(*dock, after);
        }
        if (ImGui::Button("Suggest Zones From AABBs"))
        {
            const Vec3d normal = -local->Value.Forward();
            WorldDock after = *dock;
            after.ZoneA = UniqueZoneAt(context.World.Manifest(),
                                       local->Value.Position - normal);
            after.ZoneB = UniqueZoneAt(context.World.Manifest(),
                                       local->Value.Position + normal, after.ZoneA);
            applyDock(*dock, after);
        }

        bool decorated = false;
        for (const ContentRiskRecord& record : context.World.ValidationRecords())
        {
            if (record.Kind != ContentRiskSourceKind::Dock
                || record.SourceId != dock->Id.Value)
                continue;
            const ImVec4 color = record.Severity == ContentRiskSeverity::Error
                ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                : ImVec4(1.0f, 0.7f, 0.25f, 1.0f);
            ImGui::TextColored(color, "%s", record.Message.c_str());
            decorated = true;
        }
        if (!decorated)
            ImGui::TextDisabled("Dock assignments and geometry are valid");
        return true;
    }
};
}

std::unique_ptr<IEditorComponentAdapter> MakeWorldDockEditorAdapter()
{
    return std::make_unique<WorldDockEditorAdapter>();
}
