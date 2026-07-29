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
#include <cmath>
#include <string>
#include <string_view>

namespace
{
struct DockEntityState
{
    WorldDock Dock;
    Transform3f Transform;
};

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

class DockRectangleTransaction final
    : public IValueEditTransaction<RectangleEditValue>
{
public:
    DockRectangleTransaction(EditorComponentContext context,
                             DockEntityState before)
        : Context(context), Before(std::move(before)) {}

    void Preview(const RectangleEditValue& value) override
    {
        Apply(MakeAfter(value), false);
    }

    void Commit(const RectangleEditValue& value) override
    {
        const DockEntityState after = MakeAfter(value);
        Apply(Before, true);
        auto apply = [&scene = Context.Scene, &world = Context.World,
                      entity = Context.Entity](const DockEntityState& state)
        {
            scene.SetComponent(entity, state.Dock);
            scene.SetTransform(entity, state.Transform);
            world.Revalidate();
        };
        Context.Commands.Execute(std::make_unique<ValueCommand<DockEntityState>>(
            Before, after, std::move(apply), Context.Document));
    }

    void Cancel() override
    {
        Apply(Before, true);
    }

private:
    [[nodiscard]] DockEntityState MakeAfter(
        const RectangleEditValue& value) const
    {
        DockEntityState after = Before;
        ApplyWorldDockPlaneResize(after.Dock, after.Transform,
                                  value.CenterOffset, value.HalfExtents);
        return after;
    }

    void Apply(const DockEntityState& state, bool revalidate)
    {
        Context.Scene.SetComponent(Context.Entity, state.Dock);
        Context.Scene.SetTransform(Context.Entity, state.Transform);
        if (revalidate)
            Context.World.Revalidate();
    }

    EditorComponentContext Context;
    DockEntityState Before;
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

std::string ZoneName(const WorldPartitionManifest& manifest, ZoneId id)
{
    for (const ZoneHeader& zone : manifest.Zones)
        if (zone.Id == id)
            return zone.Name.empty() ? ZoneIdToString(id) : zone.Name;
    return "unresolved";
}

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

        output.PickProxies.push_back({
            .Shape = EditorPickProxy::Kind::Rectangle,
            .Entity = context.Entity,
            .LocalToWorld = context.Transform,
            .HalfExtents = dock->HalfExtents,
        });
        if (!context.Selected)
            return;
        const DockEntityState before{ *dock, context.Transform };
        output.Rectangles.push_back(RectEditTarget{
            .Key = (dock->Id.Value << 2) | 1u,
            .LocalToWorld = context.Transform,
            .HalfExtents = dock->HalfExtents,
            .BeginEdit = [context, before]
            {
                return std::make_unique<DockRectangleTransaction>(context, before);
            },
        });
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
        if (ImGui::Button("Suggest Zones From AABBs"))
        {
            const Vec3d normal = -local->Value.Forward();
            WorldDock after = *dock;
            const std::vector<ZoneId> sideA = WorldDockZoneCandidates(
                context.World.Manifest(), local->Value.Position - normal);
            if (sideA.size() == 1)
                after.ZoneA = sideA.front();
            const std::vector<ZoneId> sideB = WorldDockZoneCandidates(
                context.World.Manifest(), local->Value.Position + normal,
                after.ZoneA);
            if (sideB.size() == 1)
                after.ZoneB = sideB.front();
            if (after.ZoneA != dock->ZoneA || after.ZoneB != dock->ZoneB)
                applyDock(*dock, after);

            SuggestionEntity = context.Entity;
            AmbiguousA = sideA.size() > 1 ? sideA : std::vector<ZoneId>{};
            AmbiguousB = sideB.size() > 1 ? sideB : std::vector<ZoneId>{};
            CandidateA = {};
            CandidateB = {};
            SuggestionStatus.clear();
            if (sideA.empty())
                SuggestionStatus += "No Side A candidate. ";
            if (sideB.empty())
                SuggestionStatus += "No Side B candidate.";
            if (!AmbiguousA.empty() || !AmbiguousB.empty())
                ImGui::OpenPopup("Resolve ambiguous Dock sides");
        }
        if (!SuggestionStatus.empty() && SuggestionEntity == context.Entity)
            ImGui::TextDisabled("%s", SuggestionStatus.c_str());

        if (ImGui::BeginPopupModal("Resolve ambiguous Dock sides", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Overlapping Zone AABBs produced multiple candidates. "
                               "Choose explicitly; the Dock references remain authoritative.");
            const auto candidateCombo = [&](const char* label,
                                            const std::vector<ZoneId>& candidates,
                                            ZoneId& choice)
            {
                if (candidates.empty())
                    return;
                const std::string preview = choice.IsValid()
                    ? ZoneName(context.World.Manifest(), choice) : "Select a Zone";
                if (ImGui::BeginCombo(label, preview.c_str()))
                {
                    for (ZoneId candidate : candidates)
                    {
                        const std::string name = ZoneName(context.World.Manifest(), candidate);
                        if (ImGui::Selectable(name.c_str(), choice == candidate))
                            choice = candidate;
                    }
                    ImGui::EndCombo();
                }
            };
            candidateCombo("Side A", AmbiguousA, CandidateA);
            candidateCombo("Side B", AmbiguousB, CandidateB);
            const bool complete = (AmbiguousA.empty() || CandidateA.IsValid())
                && (AmbiguousB.empty() || CandidateB.IsValid());
            if (!complete)
                ImGui::BeginDisabled();
            if (ImGui::Button("Apply Explicit Candidates"))
            {
                if (WorldDock* current = registry.Components.TryGet<WorldDock>(context.Entity))
                {
                    WorldDock after = *current;
                    if (CandidateA.IsValid())
                        after.ZoneA = CandidateA;
                    if (CandidateB.IsValid())
                        after.ZoneB = CandidateB;
                    applyDock(*current, after);
                }
                AmbiguousA.clear();
                AmbiguousB.clear();
                ImGui::CloseCurrentPopup();
            }
            if (!complete)
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                AmbiguousA.clear();
                AmbiguousB.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
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

private:
    mutable EntityId SuggestionEntity;
    mutable std::vector<ZoneId> AmbiguousA;
    mutable std::vector<ZoneId> AmbiguousB;
    mutable ZoneId CandidateA;
    mutable ZoneId CandidateB;
    mutable std::string SuggestionStatus;
};
}

std::unique_ptr<IEditorComponentAdapter> MakeWorldDockEditorAdapter()
{
    return std::make_unique<WorldDockEditorAdapter>();
}
