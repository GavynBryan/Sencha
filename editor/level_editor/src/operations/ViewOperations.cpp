#include "ViewOperations.h"

#include "EntityRecords.h"
#include "LevelJson.h"
#include "LevelSubjects.h"
#include "SpaceRelations.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "commands/CommandStack.h"
#include "selection/SelectionService.h"
#include "selection/commands/SelectCommand.h"
#include "viewport/CameraFraming.h"
#include "viewport/Picking.h"
#include "viewport/ViewportLayout.h"
#include "viewport/ViewportProjection.h"

#include <core/metadata/DataSchemaFields.h>
#include <math/Angle.h>
#include <math/geometry/3d/Aabb3d.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

namespace
{
    // Farther than any level is wide; an aim that hits nothing within it hits nothing.
    constexpr float kAimReach = 10000.0f;
    // How far off a surface a point is taken to stand in the air it faces.
    constexpr float kOffSurface = 0.05f;

    DataFieldSchema Root(std::vector<DataFieldSchema> members)
    {
        DataFieldSchema root = AuthoredRecordRoot();
        root.Children = std::move(members);
        return root;
    }

    EditorOperationOutcome NoView()
    {
        return EditorOperationOutcome::Refused("view.none", "no view is open");
    }

    std::string ViewName(ViewportOrientation orientation)
    {
        std::string name = Traits(orientation).Label;
        std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return name;
    }

    DataFieldSchema ViewField()
    {
        std::vector<DataEnumChoice> views;
        for (const ViewportOrientation orientation : AllViewportOrientations())
            views.push_back({ ViewName(orientation), Traits(orientation).Label, {} });
        return DataField::Enum("view", {}, "which view: perspective or an axis view", std::move(views));
    }

    JsonValue OptionalEntity(const EditorDocument& document, std::optional<EntityId> entity)
    {
        return entity ? EntityNameJson(document, *entity) : JsonValue{};
    }

    DataFieldSchema OptionalEntitySchema(std::string key, std::string summary)
    {
        return DataField::Optional(std::move(key), {}, std::move(summary), EntityNameSchema({}, {}), true);
    }

    // The ray through the middle of the view: where the designer is looking.
    Ray3d AimOf(const EditorViewport& view)
    {
        const Vec2d middle = (view.RegionMin + view.RegionMax) * 0.5f;
        return ViewportProjection(view).RayThroughPixel(middle);
    }

    // The eye of a perspective view; the middle of an axis view's back plane.
    Vec3d EyeOf(const EditorViewport& view, const Ray3d& aim)
    {
        return view.Camera.ActiveMode == EditorCamera::Mode::Perspective ? view.Camera.Position : aim.Origin;
    }

    JsonValue CameraJson(const EditorViewport& view, const Ray3d& aim)
    {
        const Vec3d forward = aim.Direction.Normalized();
        const double pitch = ToDegrees(std::asin(std::clamp(static_cast<double>(forward.Y), -1.0, 1.0)));
        return JsonValue(JsonValue::Object{
            { "view", JsonValue(ViewName(view.Orientation)) },
            { "position", PointJson(EyeOf(view, aim)) },
            { "forward", PointJson(forward) },
            { "facing", JsonValue(std::string(CompassBearing(Vec3d{ 0, 0, 0 }, forward))) },
            { "pitch", JsonValue(std::round(pitch * 10.0) / 10.0) },
        });
    }

    DataFieldSchema CameraSchema()
    {
        return DataField::Record("camera", {}, "the active view's eye", {
            ViewField(),
            DataField::Vector("position", {}, "the eye; for an axis view, the middle of the view's back plane"),
            DataField::Vector("forward", {}, "unit direction the view looks along"),
            CompassBearingField("facing", "compass direction the view looks along, north is -Z"),
            DataField::Field("pitch", DataFieldKind::Float, {}, "degrees above the horizon, negative below"),
        });
    }

    JsonValue AimJson(const EditorDocument& document, const Ray3d& aim, const Vec3d& eye)
    {
        const EditorScene& scene = document.GetScene();
        const std::optional<SurfaceHit> hit = CastRayAgainstBrushes(scene, aim, kAimReach, false);
        if (!hit)
            return JsonValue{};
        return JsonValue(JsonValue::Object{
            { "point", PointJson(hit->Point) },
            { "normal", PointJson(hit->Normal) },
            { "distance", MetresJson((hit->Point - eye).Magnitude()) },
            { "entity", OptionalEntity(document, hit->Entity.IsValid() ? std::optional(hit->Entity) : std::nullopt) },
            { "kind", JsonValue(std::string(hit->Entity.IsValid() ? EntityKind(scene, hit->Entity) : "entity")) },
            { "facing_into", OptionalEntity(document, SpaceContaining(scene, hit->Point + hit->Normal * kOffSurface)) },
        });
    }

    DataFieldSchema AimSchema()
    {
        return DataField::Optional("aim", {}, "the first solid surface at the middle of the view, null when none",
            DataField::Record({}, {}, {}, {
                DataField::Vector("point", {}, "where the aim meets the surface"),
                DataField::Vector("normal", {}, "the surface's outward normal"),
                DataField::Field("distance", DataFieldKind::Float, {}, "metres from the eye"),
                OptionalEntitySchema("entity", "what the surface belongs to"),
                DataField::Field("kind", DataFieldKind::String, {}, "space, opening, wall, volume, brush or entity"),
                OptionalEntitySchema("facing_into", "the space whose air the surface faces"),
            }),
            true);
    }

    struct Seen
    {
        EntityId Entity;
        float    Distance = 0.0f;
    };

    std::vector<Seen> SpacesInView(const EditorScene& scene, const EditorViewport& view, const Vec3d& eye)
    {
        const Frustum frustum = view.BuildRenderData().ViewFrustum;
        std::vector<Seen> seen;
        for (const EntityId entity : scene.GetAllEntities())
        {
            if (EntityKind(scene, entity) != "space")
                continue;
            const std::optional<Aabb3d> bounds = scene.EvaluatedWorldBounds(entity);
            if (!bounds || !frustum.IntersectsAabb(*bounds))
                continue;
            const Vec3d nearest{ std::clamp(eye.X, bounds->Min.X, bounds->Max.X),
                                 std::clamp(eye.Y, bounds->Min.Y, bounds->Max.Y),
                                 std::clamp(eye.Z, bounds->Min.Z, bounds->Max.Z) };
            seen.push_back({ entity, (nearest - eye).Magnitude() });
        }
        std::ranges::sort(seen, {}, &Seen::Distance);
        return seen;
    }

    EditorOperationEntry Context(ViewTarget target)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "view.context";
        entry.Definition.Category = "View";
        entry.Definition.Description =
            "What the designer is looking at in the running editor: the active view's eye and compass "
            "facing, the surface at the middle of the view, the space the eye is in, the spaces in view "
            "(nearest first) and the selection.";
        DataFieldSchema limit = DataField::Field("limit", DataFieldKind::Int, {}, "most spaces in view to list");
        limit.Default = std::int64_t{ 32 };
        limit.Numeric.Minimum = 0.0;
        entry.Definition.Parameters = Root({ std::move(limit) });
        entry.Definition.Result = Root({
            CameraSchema(),
            AimSchema(),
            OptionalEntitySchema("in_space", "the space the eye is in"),
            DataField::ArrayOf("in_view", {}, "spaces the view sees any of, nearest first",
                DataField::Record({}, {}, {}, { EntityRecordSchema("space"),
                                                DataField::Field("distance", DataFieldKind::Float, {}, "metres from the eye to its bounds") }),
                true),
            DataField::Field("truncated", DataFieldKind::Bool, {}, {}),
            SelectionSchema("selection", "what is selected"),
        });
        entry.Execute = [target](const JsonValue& arguments) {
            const EditorViewport* view = target.Layout.Active();
            if (view == nullptr)
                return NoView();
            const EditorDocument& document = target.World.FocusDocument();
            const EditorScene& scene = document.GetScene();
            const Ray3d aim = AimOf(*view);
            const Vec3d eye = EyeOf(*view, aim);
            const std::size_t most = static_cast<std::size_t>(arguments.Find("limit")->AsNumber());

            const std::vector<Seen> seen = SpacesInView(scene, *view, eye);
            JsonValue::Array inView;
            for (std::size_t index = 0; index < std::min(most, seen.size()); ++index)
                inView.push_back(JsonValue(JsonValue::Object{
                    { "space", EntityRecordJson(document, seen[index].Entity) },
                    { "distance", MetresJson(seen[index].Distance) },
                }));
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "camera", CameraJson(*view, aim) },
                { "aim", AimJson(document, aim, eye) },
                { "in_space", OptionalEntity(document, SpaceContaining(scene, eye)) },
                { "in_view", JsonValue(std::move(inView)) },
                { "truncated", JsonValue(seen.size() > most) },
                { "selection", SelectionJson(document, target.Selection.GetSelection()) },
            }));
        };
        return entry;
    }

    DataFieldSchema Subjects(std::string summary)
    {
        return DataField::ArrayOf("subjects", {}, std::move(summary),
                                  DataField::Field({}, DataFieldKind::String, {}, "an entity id or '@<name>'"));
    }

    std::optional<EditorOperationOutcome> Resolve(const EditorDocument& document, const JsonValue& arguments,
                                                  std::vector<EntityId>& entities)
    {
        const JsonValue* subjects = arguments.Find("subjects");
        SubjectResolution found = ResolveEntitySubjects(document, subjects != nullptr ? *subjects : JsonValue{});
        if (!found.Resolved())
            return SubjectRefusal(found);
        entities = std::move(found.Entities);
        return std::nullopt;
    }

    std::vector<SelectableRef> RefsOf(const EditorDocument& document, std::span<const EntityId> entities)
    {
        std::vector<SelectableRef> refs;
        for (const EntityId entity : entities)
            refs.push_back(SelectableRef::EntitySelection(document.GetRegistry().Id, entity));
        return refs;
    }

    // Frames `refs` in the active view; false when there is no view or nothing to frame.
    bool Frame(ViewTarget target, std::span<const SelectableRef> refs)
    {
        EditorViewport* view = target.Layout.Active();
        const std::optional<Aabb3d> bounds = target.World.FocusDocument().GetScene().SelectionWorldBounds(refs);
        if (view == nullptr || !bounds)
            return false;
        FrameBounds(view->Camera, *bounds);
        return true;
    }

    EditorOperationEntry Select(ViewTarget target)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "view.select";
        entry.Definition.Category = "View";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Description =
            "Selects entities in the running editor, as one undo step, and frames them in the active view "
            "when asked. No subjects clears the selection.";
        DataFieldSchema frame = DataField::Field("frame", DataFieldKind::Bool, {}, "also frame them in the active view", false);
        frame.Default = false;
        entry.Definition.Parameters = Root({ Subjects("what to select"), std::move(frame) });
        entry.Definition.Result = Root({ SelectionSchema("selection", "what is selected now"),
                                         DataField::Field("framed", DataFieldKind::Bool, {}, {}) });
        entry.Execute = [target](const JsonValue& arguments) {
            const EditorDocument& document = target.World.FocusDocument();
            std::vector<EntityId> entities;
            if (std::optional<EditorOperationOutcome> refusal = Resolve(document, arguments, entities))
                return *refusal;
            SelectionSnapshot next;
            next.Items = RefsOf(document, entities);
            if (!next.Items.empty())
                next.Primary = next.Items.front();
            target.Commands.Execute(std::make_unique<SelectCommand>(target.Selection, std::move(next)));
            const std::span<const SelectableRef> selected = target.Selection.GetSelection();
            const bool framed = arguments.Find("frame")->AsBool() && Frame(target, selected);
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "selection", SelectionJson(document, selected) },
                { "framed", JsonValue(framed) },
            }));
        };
        return entry;
    }

    EditorOperationEntry FrameEntry(ViewTarget target)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "view.frame";
        entry.Definition.Category = "View";
        entry.Definition.Description =
            "Turns the active view onto subjects, or onto the selection when none are named, without "
            "changing the selection.";
        entry.Definition.Parameters = Root({ Subjects("what to frame; the selection when empty") });
        entry.Definition.Result = Root({ CameraSchema() });
        entry.Execute = [target](const JsonValue& arguments) {
            const EditorDocument& document = target.World.FocusDocument();
            std::vector<EntityId> entities;
            if (std::optional<EditorOperationOutcome> refusal = Resolve(document, arguments, entities))
                return *refusal;
            const std::vector<SelectableRef> named = RefsOf(document, entities);
            const std::span<const SelectableRef> refs =
                named.empty() ? target.Selection.GetSelection() : std::span<const SelectableRef>(named);
            if (target.Layout.Active() == nullptr)
                return NoView();
            if (!Frame(target, refs))
                return EditorOperationOutcome::Refused("view.nothing_to_frame", "nothing named or selected has bounds");
            const EditorViewport& view = *target.Layout.Active();
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "camera", CameraJson(view, AimOf(view)) } }));
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> ViewOperations(ViewTarget target)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Context(target));
    entries.push_back(Select(target));
    entries.push_back(FrameEntry(target));
    return entries;
}
