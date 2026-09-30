#include "WorldDockEditorAdapter.h"

#include "EditorTheme.h"
#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDockAuthoring.h"
#include "document/WorldDocument.h"
#include "document/commands/ValueCommand.h"
#include "properties/SettingProperties.h"
#include "selection/SelectionService.h"

#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
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

    bool AuthorsViewportAffordances() const override { return true; }

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

    bool AuthorsInspectorRows() const override { return true; }

    std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
    {
        const Registry& registry = std::as_const(context.Scene.GetRegistry());
        const WorldDock* dock = registry.Components.TryGet<WorldDock>(context.Entity);
        if (dock == nullptr || registry.Components.TryGet<LocalTransform>(context.Entity) == nullptr)
            return {};
        const WorldPartitionManifest& manifest = context.World.Manifest();

        PropertyNode group;
        group.Key = "dock";
        group.Descriptor.Kind = PropertyKind::Group;
        group.Descriptor.Label = "Dock";
        group.Descriptor.Verbs = { { .Id = "swap", .Label = "Swap Sides" },
                                   { .Id = "suggest", .Label = "Suggest Zones From Bounds" } };

        PropertyNode id = NoteProperty("id", "Id", std::format("{:016x}", dock->Id.Value));
        id.Descriptor.Verbs.push_back({ .Id = "generate", .Label = "Generate" });
        group.Children.push_back(std::move(id));
        group.Children.push_back(ZoneChoice("zone_a", "Zone A", manifest, dock->ZoneA));
        group.Children.push_back(ZoneChoice("zone_b", "Zone B", manifest, dock->ZoneB));
        group.Children.push_back(ChoiceProperty("directions", "Directions", kDirections, DirectionIndex(dock->Directions)));
        const std::array<double, 2> size{ dock->HalfExtents.X * 2.0, dock->HalfExtents.Y * 2.0 };
        group.Children.push_back(VectorProperty("size", "Width, Height", size));

        // Bounds that overlap leave the suggestion ambiguous: the person picks.
        if (SuggestionEntity == context.Entity)
        {
            if (!AmbiguousA.empty())
                group.Children.push_back(CandidateChoice("candidate_a", "Side A could be", manifest, AmbiguousA));
            if (!AmbiguousB.empty())
                group.Children.push_back(CandidateChoice("candidate_b", "Side B could be", manifest, AmbiguousB));
            if (!SuggestionStatus.empty())
                group.Children.push_back(NoteProperty("suggestion", "Suggestion", SuggestionStatus));
        }

        bool reported = false;
        for (const ContentRiskRecord& record : context.World.ValidationRecords())
        {
            if (record.Kind != ContentRiskSourceKind::Dock || record.SourceId != dock->Id.Value)
                continue;
            group.Children.push_back(NoteProperty("risk" + std::to_string(group.Children.size()),
                                          record.Severity == ContentRiskSeverity::Error ? "Error" : "Warning",
                                          record.Message));
            reported = true;
        }
        if (!reported)
            group.Children.push_back(NoteProperty("valid", "Checked", "Assignments and geometry are valid"));
        return { std::move(group) };
    }

    PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                         std::string_view value) const override
    {
        const WorldDock* dock = std::as_const(context.Scene.GetRegistry()).Components.TryGet<WorldDock>(context.Entity);
        if (dock == nullptr)
            return PropertyEditResult::Gone;
        const WorldPartitionManifest& manifest = context.World.Manifest();
        WorldDock after = *dock;
        if (key == "zone_a" || key == "zone_b" || key == "candidate_a" || key == "candidate_b")
        {
            const std::optional<ZoneId> zone = ZoneNamed(manifest, value);
            if (!zone.has_value())
                return PropertyEditResult::Refused;
            (key.ends_with("_a") ? after.ZoneA : after.ZoneB) = *zone;
            if (key.starts_with("candidate"))
                (key.ends_with("_a") ? AmbiguousA : AmbiguousB).clear();
        }
        else if (key == "directions")
        {
            const std::optional<std::size_t> choice = ParseChoice(value, kDirections);
            if (!choice.has_value())
                return PropertyEditResult::Refused;
            after.Directions = *choice == 0 ? DockDirectionBoth : *choice == 1 ? DockDirectionAToB : DockDirectionBToA;
        }
        else if (key == "size")
        {
            const std::optional<std::vector<double>> size = ParseVector(value, 2);
            if (!size.has_value())
                return PropertyEditResult::Refused;
            after.HalfExtents = { std::max(0.025f, static_cast<float>((*size)[0]) * 0.5f),
                                  std::max(0.025f, static_cast<float>((*size)[1]) * 0.5f) };
        }
        else
        {
            return PropertyEditResult::Gone;
        }
        ApplyDock(context, *dock, after);
        return PropertyEditResult::Applied;
    }

    PropertyEditResult ActInspector(EditorComponentInspectorContext& context, std::string_view key,
                                    std::string_view verb, std::string_view) const override
    {
        Registry& registry = context.Scene.GetRegistry();
        const WorldDock* dock = std::as_const(registry).Components.TryGet<WorldDock>(context.Entity);
        const LocalTransform* local = std::as_const(registry).Components.TryGet<LocalTransform>(context.Entity);
        if (dock == nullptr || local == nullptr)
            return PropertyEditResult::Gone;
        if (key == "id" && verb == "generate")
        {
            WorldDock after = *dock;
            after.Id = context.World.MintDockId();
            ApplyDock(context, *dock, after);
            return PropertyEditResult::Applied;
        }
        if (key == "dock" && verb == "swap")
        {
            const DockEntityState before{ *dock, local->Value };
            DockEntityState after = before;
            SwapWorldDockSides(after.Dock, after.Transform);
            context.Commands.Execute(std::make_unique<ValueCommand<DockEntityState>>(
                before, after,
                [&scene = context.Scene, &world = context.World, entity = context.Entity](const DockEntityState& value)
                {
                    scene.SetComponent(entity, value.Dock);
                    scene.SetTransform(entity, value.Transform);
                    world.Revalidate();
                },
                context.Document));
            return PropertyEditResult::Applied;
        }
        if (key == "dock" && verb == "suggest")
        {
            Suggest(context, *dock, local->Value);
            return PropertyEditResult::Applied;
        }
        return PropertyEditResult::Gone;
    }

private:
    static constexpr std::array<std::string_view, 3> kDirections = { "Both", "A to B", "B to A" };

    static std::size_t DirectionIndex(std::uint8_t directions)
    {
        return directions == DockDirectionBoth ? 0 : directions == DockDirectionAToB ? 1 : 2;
    }

    static PropertyNode ZoneChoice(std::string key, std::string label, const WorldPartitionManifest& manifest, ZoneId current)
    {
        PropertyNode node;
        node.Key = std::move(key);
        node.Descriptor.Kind = PropertyKind::Choice;
        node.Descriptor.Label = std::move(label);
        for (const ZoneHeader& zone : manifest.Zones)
            node.Descriptor.Choices.push_back(zone.Name);
        node.Value = ZoneName(manifest, current);
        if (std::ranges::find(node.Descriptor.Choices, node.Value) == node.Descriptor.Choices.end())
            node.Descriptor.Choices.push_back(node.Value);
        return node;
    }

    static PropertyNode CandidateChoice(std::string key, std::string label, const WorldPartitionManifest& manifest,
                                        const std::vector<ZoneId>& candidates)
    {
        PropertyNode node;
        node.Key = std::move(key);
        node.Descriptor.Kind = PropertyKind::Choice;
        node.Descriptor.Label = std::move(label);
        node.Descriptor.Choices.push_back("Choose a zone");
        for (const ZoneId candidate : candidates)
            node.Descriptor.Choices.push_back(ZoneName(manifest, candidate));
        node.Value = node.Descriptor.Choices.front();
        return node;
    }

    static std::optional<ZoneId> ZoneNamed(const WorldPartitionManifest& manifest, std::string_view name)
    {
        for (const ZoneHeader& zone : manifest.Zones)
            if (zone.Name == name)
                return zone.Id;
        return std::nullopt;
    }

    static void ApplyDock(EditorComponentInspectorContext& context, const WorldDock& before, const WorldDock& after)
    {
        context.Commands.Execute(std::make_unique<ValueCommand<WorldDock>>(
            before, after,
            [&scene = context.Scene, &world = context.World, entity = context.Entity](const WorldDock& value)
            {
                scene.SetComponent(entity, value);
                world.Revalidate();
            },
            context.Document));
    }

    void Suggest(EditorComponentInspectorContext& context, const WorldDock& dock, const Transform3f& transform) const
    {
        const Vec3d normal = -transform.Forward();
        const WorldPartitionManifest& manifest = context.World.Manifest();
        WorldDock after = dock;
        const std::vector<ZoneId> sideA = WorldDockZoneCandidates(manifest, transform.Position - normal);
        if (sideA.size() == 1)
            after.ZoneA = sideA.front();
        const std::vector<ZoneId> sideB = WorldDockZoneCandidates(manifest, transform.Position + normal, after.ZoneA);
        if (sideB.size() == 1)
            after.ZoneB = sideB.front();
        if (after.ZoneA != dock.ZoneA || after.ZoneB != dock.ZoneB)
            ApplyDock(context, dock, after);

        SuggestionEntity = context.Entity;
        AmbiguousA = sideA.size() > 1 ? sideA : std::vector<ZoneId>{};
        AmbiguousB = sideB.size() > 1 ? sideB : std::vector<ZoneId>{};
        SuggestionStatus.clear();
        if (sideA.empty())
            SuggestionStatus += "No Side A candidate. ";
        if (sideB.empty())
            SuggestionStatus += "No Side B candidate.";
    }

    mutable EntityId SuggestionEntity;
    mutable std::vector<ZoneId> AmbiguousA;
    mutable std::vector<ZoneId> AmbiguousB;
    mutable std::string SuggestionStatus;
};
}

std::unique_ptr<IEditorComponentAdapter> MakeWorldDockEditorAdapter()
{
    return std::make_unique<WorldDockEditorAdapter>();
}
