#include "BrushOperations.h"

#include "EntityRecords.h"
#include "LevelJson.h"
#include "LevelSubjects.h"
#include "LevelTarget.h"
#include "SpaceEdits.h"
#include "SpaceRelations.h"

#include "brush/BrushClipCut.h"
#include "brush/BrushFaceFrame.h"
#include "brush/CarvePolygon.h"
#include "brush/CarveShape.h"
#include "brush/CarveStatus.h"
#include "brush/CarveSurround.h"
#include "document/EditorDocument.h"
#include "document/EditorEntityRecipe.h"
#include "document/EditorScene.h"
#include "document/EntityBranches.h"
#include "document/EntityChangeSet.h"
#include "document/LevelConventions.h"
#include "document/commands/RenameEntityCommand.h"
#include "meshedit/MeshEditService.h"
#include "selection/SelectableRef.h"
#include "viewport/ClipPlaneMath.h"

#include <core/metadata/DataSchemaFields.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr float kSmallest = 0.01f;

    DataFieldSchema Root(std::vector<DataFieldSchema> members)
    {
        return DataField::Record({}, {}, {}, std::move(members));
    }

    EditorOperationEntry Box(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "brush.box";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Brush";
        entry.Definition.Description =
            "Adds a solid box brush between two corners, as one undoable step: a pillar, a ledge, a crate. "
            "Its faces take the project's floor, ceiling and wall materials by which way they face.";
        entry.Definition.Parameters = Root({
            DataField::Vector("min", {}, "one corner"),
            DataField::Vector("max", {}, "the opposite corner"),
            DataField::Field("name", DataFieldKind::String, {}, "what to call it; the next free 'Brush' name when absent", false),
        });
        entry.Definition.Result = MutationResultSchema({ EntityNameSchema("brush", "the brush made") });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const JsonValue* wanted = arguments.Find("name");
                std::variant<std::string, EditorOperationOutcome> checked =
                    CheckEntityName(document, EntityId{}, false, wanted != nullptr ? wanted->AsString() : std::string{});
                if (auto* refused = std::get_if<EditorOperationOutcome>(&checked))
                    return std::move(*refused);
                const std::string& given = std::get<std::string>(checked);
                const Vec3d a = PointFromJson(*arguments.Find("min"));
                const Vec3d b = PointFromJson(*arguments.Find("max"));
                const Vec3d low{ std::min(a.X, b.X), std::min(a.Y, b.Y), std::min(a.Z, b.Z) };
                const Vec3d high{ std::max(a.X, b.X), std::max(a.Y, b.Y), std::max(a.Z, b.Z) };
                const Vec3d size = high - low;
                if (size.X < kSmallest || size.Y < kSmallest || size.Z < kSmallest)
                    return EditorOperationOutcome::Refused("brush.too_small", "the corners must be at least 1 cm apart on every axis");

                EditorScene& scene = document.GetScene();
                const EntityId brush = scene.CreateBrush((low + high) * 0.5f, size * 0.5f);
                BrushMesh mesh = *scene.TryGetBrushMesh(brush);
                PaintByConvention(mesh, ConventionsOf(document));
                scene.SetBrushMesh(brush, std::move(mesh));
                if (!given.empty())
                    if (auto rename = MakeRenameEntityCommand(brush, given, scene, document))
                        rename->Execute();
                recorder.Created(brush);
                document.MarkDirty();
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "brush", EntityNameJson(document, brush) } }));
            });
        };
        return entry;
    }
}

namespace
{
    // The solid brush a subject names, or the refusal.
    std::optional<EditorOperationOutcome> SolidBrush(const EditorDocument& document, const JsonValue& arguments,
                                                     EntityId& brush)
    {
        const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
        if (!subject.Resolved())
            return SubjectRefusal(subject);
        brush = subject.Entities.front();
        const EditorScene& scene = document.GetScene();
        if (scene.TryGetBrushMesh(brush) == nullptr || scene.RoleOf(brush) != BrushRole::Solid)
            return EditorOperationOutcome::Refused("brush.not_solid",
                                                   "only a solid brush is reshaped here; a space's faces move with space.resize_face");
        return std::nullopt;
    }

    DataFieldSchema Subject()
    {
        return DataField::Field("subject", DataFieldKind::String, {}, "a brush's id or '@<name>'");
    }

    EditorOperationEntry Extrude(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "brush.extrude";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Brush";
        entry.Definition.Description =
            "Pulls the face of a brush that looks toward a direction out by a distance, growing a new cap and "
            "side walls, as one undoable step: a step, a lip, an arm of an L.";
        entry.Definition.Parameters = Root({
            Subject(),
            AxisDirectionField("face", "the face looking this way"),
            DataField::Field("distance", DataFieldKind::Float, {}, "metres outward; negative pushes in"),
        });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                EntityId brush;
                if (std::optional<EditorOperationOutcome> refusal = SolidBrush(document, arguments, brush))
                    return *refusal;
                EditorScene& scene = document.GetScene();
                const std::optional<std::uint32_t> face =
                    FaceToward(scene, brush, *AxisDirection(arguments.Find("face")->AsString()));
                if (!face)
                    return EditorOperationOutcome::Refused("brush.no_face", "the brush has no face looking that way");
                const float distance = static_cast<float>(arguments.Find("distance")->AsNumber());
                const Vec3d normal = WorldFaceOf(scene, brush, *face)->Normal;
                const SelectableRef ref = SelectableRef::FaceSelection(scene.GetRegistry().Id, brush, *face);
                std::optional<MeshEditService::ExtrudeResult> extruded = MeshEditService{}.ExtrudeElements(
                    *scene.TryGetBrushMesh(brush), *scene.TryGetWorldTransform(brush), std::span(&ref, 1),
                    MeshElementKind::Face, normal * distance, true);
                if (!extruded)
                    return EditorOperationOutcome::Refused("brush.unusable", "the extruded brush would not be usable geometry");
                recorder.Touch(brush);
                scene.SetBrushMesh(brush, std::move(extruded->Mesh));
                document.MarkDirty();
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }
}

namespace
{
    // The brush keeps the first piece; every other becomes a copy of the brush
    // alone, in its place, holding that piece. All of them, in order.
    std::vector<EntityId> Split(EditorDocument& document, WorldDocument& world, EntityChangeRecorder& recorder,
                                EntityId brush, std::vector<BrushMesh> pieces)
    {
        EditorScene& scene = document.GetScene();
        std::vector<EntityId> made{ brush };
        const Transform3f placement = *scene.TryGetWorldTransform(brush);
        for (std::size_t index = 1; index < pieces.size(); ++index)
        {
            const BranchCopy copy = CaptureBranchCopy(document, std::span(&brush, 1), DuplicateBranchPolicy::EntityOnly,
                [&](std::vector<EntitySnapshot>& snapshots) {
                    if (snapshots.front().Brush)
                        snapshots.front().Brush->Mesh = std::move(pieces[index]);
                    RemapWorldConnectionDuplicateSnapshots(world, snapshots);
                });
            for (const EntityId piece : RestoreBranchCopy(document, copy, std::span(&placement, 1), false))
            {
                recorder.Created(piece);
                made.push_back(piece);
            }
        }
        recorder.Touch(brush);
        scene.SetBrushMesh(brush, std::move(pieces.front()));
        return made;
    }

    EditorOperationEntry Clip(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "brush.clip";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Brush";
        entry.Definition.Description =
            "Cuts brushes with a plane, as one undoable step, keeping the side the normal points to, the other "
            "side, or both as separate brushes. A kept part that comes apart becomes a brush per part.";
        DataFieldSchema keep = DataField::Enum("keep", {}, "what stays",
            { { "front", "Front", "the side the normal points to" },
              { "back", "Back", "the other side" },
              { "both", "Both", "both sides, as separate brushes" } },
            false);
        keep.Default = std::string("front");
        DataFieldSchema capField = DataField::Field("cap", DataFieldKind::Bool, {}, "close the cut faces", false);
        capField.Default = true;
        entry.Definition.Parameters = Root({
            DataField::ArrayOf("subjects", {}, "the brushes to cut",
                               DataField::Field({}, DataFieldKind::String, {}, "an entity id or '@<name>'"), true),
            DataField::Vector("point", {}, "a point on the cutting plane"),
            DataField::Vector("normal", {}, "the plane's front direction"),
            std::move(keep),
            std::move(capField),
        });
        entry.Definition.Result = MutationResultSchema({
            DataField::ArrayOf("pieces", {}, "every brush the cut left, in subject order", EntityNameSchema({}, {}), true) });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            WorldDocument& world = level.LevelWorld();
            return level.Mutate(name, arguments, [&arguments, &world](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subjects = ResolveEntitySubjects(document, *arguments.Find("subjects"));
                if (!subjects.Resolved())
                    return SubjectRefusal(subjects);
                const Vec3d normal = PointFromJson(*arguments.Find("normal"));
                if (normal.Magnitude() < 1e-4f)
                    return EditorOperationOutcome::Refused("clip.no_normal", "the normal has no direction");
                const Plane plane = Plane::FromNormalAndPoint(normal.Normalized(), PointFromJson(*arguments.Find("point")));
                const Vec3d front = PointFromJson(*arguments.Find("point")) + normal.Normalized();
                const std::string& kept = arguments.Find("keep")->AsString();
                const ClipMode mode = kept == "both" ? ClipMode::Split : kept == "back" ? ClipMode::KeepBack : ClipMode::KeepFront;
                const BrushOps::ClipCap cap = arguments.Find("cap")->AsBool() ? BrushOps::ClipCap::Capped : BrushOps::ClipCap::Open;

                EditorScene& scene = document.GetScene();
                std::vector<std::pair<EntityId, std::vector<BrushMesh>>> cuts;
                for (const EntityId brush : subjects.Entities)
                {
                    if (scene.TryGetBrushMesh(brush) == nullptr || scene.RoleOf(brush) != BrushRole::Solid)
                        return EditorOperationOutcome::Refused("brush.not_solid", "only solid brushes are cut");
                    const Plane local = ClipPlaneMath::InLocal(plane, front, *scene.TryGetWorldTransform(brush));
                    const ClipCut cut = CutBrush(*scene.TryGetBrushMesh(brush), local, cap);
                    if (cut.Outcome == ClipCut::Result::Invalid)
                        return EditorOperationOutcome::Refused("clip.failed", EntityNameText(document, brush) + ": " + cut.Why);
                    if (cut.Outcome == ClipCut::Result::Valid)
                        cuts.emplace_back(brush, KeptShells(cut, mode));
                }
                if (cuts.empty())
                    return EditorOperationOutcome::Refused("clip.missed", "the plane crosses none of them");

                JsonValue::Array pieces;
                for (auto& [brush, shells] : cuts)
                    for (const EntityId piece : Split(document, world, recorder, brush, std::move(shells)))
                        pieces.push_back(EntityNameJson(document, piece));
                document.MarkDirty();
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "pieces", JsonValue(std::move(pieces)) } }));
            });
        };
        return entry;
    }
}

namespace
{
    // How far off a face a carve's point may be and still name it.
    constexpr float kCarveReach = 0.05f;

    // The face of `brush` that `at` lies on or just off, nearest first.
    std::optional<std::uint32_t> FaceAt(const EditorScene& scene, EntityId brush, const Vec3d& at)
    {
        std::optional<std::uint32_t> best;
        float bestDepth = kCarveReach;
        const BrushMesh& mesh = *scene.TryGetBrushMesh(brush);
        for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
        {
            const std::optional<WorldFace> face = WorldFaceOf(scene, brush, f);
            if (!face || !face->Over(at))
                continue;
            if (const float depth = std::abs(face->Normal.Dot(at - face->Corners.front())); depth <= bestDepth)
            {
                best = f;
                bestDepth = depth;
            }
        }
        return best;
    }

    EditorOperationEntry Carve(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "brush.carve";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Brush";
        entry.Definition.Description =
            "Cuts a rectangle or an arch into the face of a brush at a point, through the brush or as a recess, "
            "as one undoable step: a window in a detached wall, an alcove, a hole in a floor. The shape stands "
            "upright on a wall. Doors in generated walls are opening.create.";
        DataFieldSchema shape = DataField::Enum("shape", {}, "the outline",
            { { "rectangle", "Rectangle", {} }, { "arch", "Arch", "a rectangle under a round top" } }, false);
        shape.Default = std::string("rectangle");
        DataFieldSchema rise = DataField::Field("rise", DataFieldKind::Float, {}, "an arch's curve as a share of its height, 0 to 1", false);
        rise.Default = 0.5;
        rise.Numeric.Minimum = 0.0;
        rise.Numeric.Maximum = 1.0;
        DataFieldSchema through = DataField::Field("through", DataFieldKind::Bool, {}, "cut through to the far side; a recess otherwise", false);
        through.Default = true;
        entry.Definition.Parameters = Root({
            Subject(),
            DataField::Vector("at", {}, "the middle of the cut, on the face"),
            DataField::Field("width", DataFieldKind::Float, {}, "metres across"),
            DataField::Field("height", DataFieldKind::Float, {}, "metres up the face"),
            std::move(shape),
            std::move(rise),
            std::move(through),
        });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                EntityId brush;
                if (std::optional<EditorOperationOutcome> refusal = SolidBrush(document, arguments, brush))
                    return *refusal;
                const float width = static_cast<float>(arguments.Find("width")->AsNumber());
                const float height = static_cast<float>(arguments.Find("height")->AsNumber());
                if (width < kSmallest || height < kSmallest)
                    return EditorOperationOutcome::Refused("carve.too_small", "a cut is at least 1 cm across and up");
                EditorScene& scene = document.GetScene();
                const Vec3d at = PointFromJson(*arguments.Find("at"));
                const std::optional<std::uint32_t> face = FaceAt(scene, brush, at);
                if (!face)
                    return EditorOperationOutcome::Refused("carve.no_face", "the point is on no face of the brush");

                const BrushMesh& mesh = *scene.TryGetBrushMesh(brush);
                const Transform3f& placed = *scene.TryGetWorldTransform(brush);
                const BrushFaceFrameResult framed = FaceFrame(mesh, *face, kCarvePlanarTolerance);
                if (!framed.Frame)
                    return EditorOperationOutcome::Refused("carve.refused", CarveStatusText(framed.Status));
                const BrushFaceFrame& frame = *framed.Frame;

                // The shape is laid out in the world, upright along the face's
                // own up, and carried into the brush's frame point by point.
                const Vec3d across = placed.TransformVector(frame.AxisU).Normalized();
                const Vec3d up = placed.TransformVector(frame.AxisV).Normalized();
                CarveShapeParams params;
                params.ArchRise = static_cast<float>(arguments.Find("rise")->AsNumber());
                const CarveShape kind = arguments.Find("shape")->AsString() == "arch" ? CarveShape::Arch : CarveShape::Rectangle;
                std::vector<Vec2d> outline;
                for (const Vec2d& point : CarveShapeOutline(kind, Vec2d{ -0.5f * width, -0.5f * height },
                                                            Vec2d{ 0.5f * width, 0.5f * height }, params))
                    outline.push_back(frame.ToFrame(placed.InverseTransformPoint(at + across * point.X + up * point.Y)));
                if (PolygonSignedArea(outline) < 0.0f)
                    std::ranges::reverse(outline);

                std::vector<FaceCorners> surface;
                for (const std::uint32_t piece : CoplanarSurface(mesh, *face, kCarvePlanarTolerance))
                    surface.push_back(CornersOf(mesh, piece));
                CarveOutcome carved = CarveAcrossSurface(mesh, surface, frame, outline,
                                                         arguments.Find("through")->AsBool(), kCarvePlanarTolerance);
                if (!carved.Ok())
                    return EditorOperationOutcome::Refused("carve.refused", CarveStatusText(carved.Status()));
                recorder.Touch(brush);
                scene.SetBrushMesh(brush, std::move(carved.Take().Mesh));
                document.MarkDirty();
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> BrushOperations(LevelTarget& level)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Box(level));
    entries.push_back(Extrude(level));
    entries.push_back(Clip(level));
    entries.push_back(Carve(level));
    return entries;
}
