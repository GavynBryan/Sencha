#include "SpaceOperations.h"

#include "LevelJson.h"
#include "LevelSubjects.h"
#include "LevelTarget.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "document/LevelConventions.h"
#include "document/SpaceComponents.h"
#include "document/commands/RenameEntityCommand.h"

#include <core/metadata/DataSchemaFields.h>
#include <world/registry/Registry.h>

#include <cmath>
#include <optional>
#include <string>

namespace
{
    constexpr float kSmallest = 0.1f;
    constexpr float kOpeningReach = 0.5f;

    LevelConventions ConventionsOf(const EditorDocument& document)
    {
        return LevelConventionsFrom(FindLevelConventions(document.ContentRoots()).Values);
    }

    JsonValue Named(const EditorDocument& document, EntityId entity)
    {
        return JsonValue(JsonValue::Object{
            { "id", JsonValue(PersistentIdText(document, entity)) },
            { "name", JsonValue(EntityNameText(document, entity)) },
        });
    }

    DataFieldSchema NamedSchema(std::string key, std::string summary)
    {
        return DataField::Record(std::move(key), {}, std::move(summary), {
            DataField::Field("id", DataFieldKind::Entity, {}, {}),
            DataField::Field("name", DataFieldKind::String, {}, {}),
        });
    }

    std::optional<float> NumberOf(const JsonValue& arguments, std::string_view key)
    {
        const JsonValue* value = arguments.Find(key);
        return value != nullptr && value->IsNumber() ? std::optional<float>(static_cast<float>(value->AsNumber()))
                                                     : std::nullopt;
    }

    void Rename(EditorDocument& document, EntityId entity, std::string_view name)
    {
        if (auto rename = MakeRenameEntityCommand(entity, name, document.GetScene(), document))
            rename->Execute();
    }

    // Floors take the floor material, ceilings the ceiling's, the rest the walls'.
    void PaintByConvention(BrushMesh& mesh, const LevelConventions& conventions)
    {
        for (BrushFace& face : mesh.Faces)
        {
            const float up = BrushComputeFaceNormal(mesh, face).Y;
            const std::optional<AssetRef>& material = up < -0.7f ? conventions.FloorMaterial
                : up > 0.7f                                       ? conventions.CeilingMaterial
                                                                  : conventions.WallMaterial;
            if (material)
                face.Material.Material = *material;
        }
    }

    EditorOperationEntry Create(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "space.create";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Space";
        entry.Definition.Description =
            "Adds a named box-shaped space between two corners, with generated walls unless asked not to. "
            "Its faces take the project's floor, ceiling and wall materials.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("name", DataFieldKind::String, {}, "what to call it, unique in the level"),
            DataField::Vector("min", {}, "the corner with the lowest x, y and z; y is the floor"),
            DataField::Vector("max", {}, "the opposite corner; y is the ceiling"),
            DataField::Optional("shell", {}, "generate walls; true when absent",
                                DataField::Field({}, DataFieldKind::Bool, {}, {})),
            DataField::Optional("thickness", {}, "wall thickness in metres; the project's when absent",
                                DataField::Field({}, DataFieldKind::Float, {}, {})),
        });
        entry.Definition.Result = MutationResultSchema({ NamedSchema("space", "the space made") });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const std::string& spaceName = arguments.Find("name")->AsString();
                if (spaceName.empty())
                    return EditorOperationOutcome::Refused("space.name_invalid", "a space needs a name");
                if (ResolveEntitySubject(document, "@" + spaceName).Resolved())
                    return EditorOperationOutcome::Refused("space.name_taken",
                                                           "something is already called '" + spaceName + "'");
                const Vec3d min = PointFromJson(*arguments.Find("min"));
                const Vec3d max = PointFromJson(*arguments.Find("max"));
                const Vec3d size = max - min;
                if (size.X < kSmallest || size.Y < kSmallest || size.Z < kSmallest)
                    return EditorOperationOutcome::Refused("space.too_small",
                                                           "max must exceed min by at least 0.1 m on every axis");

                const LevelConventions conventions = ConventionsOf(document);
                EditorScene& scene = document.GetScene();
                const EntityId space = scene.CreateBrush((min + max) * 0.5f, size * 0.5f);
                BrushMesh mesh = *scene.TryGetBrushMesh(space);
                PaintByConvention(mesh, conventions);
                scene.SetBrushMesh(space, std::move(mesh));
                World& components = scene.GetRegistry().Components;
                components.AddComponent(space, BrushVolume{});
                components.AddComponent(space, Space{});
                const JsonValue* shell = arguments.Find("shell");
                if (shell == nullptr || !shell->IsBool() || shell->AsBool())
                    components.AddComponent(
                        space, Shell{ .Thickness = NumberOf(arguments, "thickness").value_or(conventions.ShellThickness) });
                Rename(document, space, spaceName);
                recorder.Created(space);
                document.MarkDirty();
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "space", Named(document, space) } }));
            });
        };
        return entry;
    }

    struct WallHit
    {
        EntityId Space;
        Vec3d    Outward;
        Vec3d    OnWall;   // the point carried onto the middle of the wall
        float    FloorY = 0.0f;
    };

    // The generated wall `point` is in or nearest: a side face of a shelled
    // space whose slab, half a metre either way, holds the point, and whose
    // face the point lies over.
    std::optional<WallHit> WallAt(const EditorScene& scene, const Vec3d& point)
    {
        std::optional<WallHit> best;
        float bestOff = 0.0f;
        const World& components = scene.GetRegistry().Components;
        for (const EntityId entity : scene.GetAllEntities())
        {
            const Shell* shell = components.TryGet<Shell>(entity);
            const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
            const Transform3f* world = scene.TryGetWorldTransform(entity);
            if (shell == nullptr || mesh == nullptr || world == nullptr || scene.RoleOf(entity) != BrushRole::Volume)
                continue;
            for (const BrushFace& face : mesh->Faces)
            {
                std::vector<Vec3d> corners;
                for (const std::uint32_t v : face.Loop)
                    corners.push_back(world->TransformPoint(mesh->Vertices[v].Position));
                Vec3d normal{ 0, 0, 0 };
                for (std::size_t i = 0; i < corners.size(); ++i)
                    normal = normal + corners[i].Cross(corners[(i + 1) % corners.size()]);
                normal = normal.Normalized();
                if (std::abs(normal.Y) > 0.7f)
                    continue;
                const float depth = normal.Dot(point - corners.front());
                if (depth < -kOpeningReach || depth > shell->Thickness + kOpeningReach)
                    continue;
                const Vec3d onFace = point - normal * depth;
                bool inside = true;
                for (std::size_t i = 0; i < corners.size() && inside; ++i)
                    inside = (corners[(i + 1) % corners.size()] - corners[i]).Cross(onFace - corners[i]).Dot(normal)
                        >= -1e-3f;
                if (!inside)
                    continue;
                const float off = std::abs(depth - 0.5f * shell->Thickness);
                if (best && off >= bestOff)
                    continue;
                float floorY = corners.front().Y;
                for (const Vec3d& corner : corners)
                    floorY = std::min(floorY, corner.Y);
                best = WallHit{ entity, normal, onFace + normal * (0.5f * shell->Thickness), floorY };
                bestOff = off;
            }
        }
        return best;
    }

    EditorOperationEntry CreateOpening(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "opening.create";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Space";
        entry.Definition.Description =
            "Cuts a door or window through the generated wall at a point, owned by the space whose wall it "
            "is. A sill of zero, the default, makes a doorway down to that wall's floor.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Vector("at", {}, "a point in or near the wall; the opening is centred across from it"),
            DataField::Optional("sill", {}, "metres above the wall's floor; 0 when absent",
                                DataField::Field({}, DataFieldKind::Float, {}, {})),
            DataField::Optional("width", {}, "metres; the project's opening width when absent",
                                DataField::Field({}, DataFieldKind::Float, {}, {})),
            DataField::Optional("height", {}, "metres; the project's opening height when absent",
                                DataField::Field({}, DataFieldKind::Float, {}, {})),
            DataField::Optional("shape", {}, "rectangle when absent",
                                DataField::Enum({}, {}, {}, { { "rectangle", "Rectangle", {} }, { "arch", "Arch", {} } })),
            DataField::Optional("name", {}, "what to call it; after its space when absent",
                                DataField::Field({}, DataFieldKind::String, {}, {})),
        });
        entry.Definition.Result = MutationResultSchema({
            NamedSchema("opening", "the opening made"),
            NamedSchema("space", "the space whose wall it cuts and that owns it"),
        });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                EditorScene& scene = document.GetScene();
                const std::optional<WallHit> wall = WallAt(scene, PointFromJson(*arguments.Find("at")));
                if (!wall)
                    return EditorOperationOutcome::Refused("opening.no_wall",
                                                           "no generated wall of a space is at that point");
                const LevelConventions conventions = ConventionsOf(document);
                Opening opening;
                opening.Width = NumberOf(arguments, "width").value_or(conventions.OpeningWidth);
                opening.Height = NumberOf(arguments, "height").value_or(conventions.OpeningHeight);
                if (opening.Width < kSmallest || opening.Height < kSmallest)
                    return EditorOperationOutcome::Refused("opening.too_small", "width and height must be at least 0.1 m");
                const JsonValue* shape = arguments.Find("shape");
                opening.Shape = shape != nullptr && shape->IsString() && shape->AsString() == "arch" ? CarveShape::Arch
                                                                                                      : CarveShape::Rectangle;
                const float sill = NumberOf(arguments, "sill").value_or(0.0f);

                const Vec3d up{ 0, 1, 0 };
                Transform3f frame;
                frame.Position = Vec3d{ wall->OnWall.X, wall->FloorY + sill, wall->OnWall.Z };
                frame.Rotation = Quatf::FromBasis(up.Cross(wall->Outward), up, wall->Outward);
                const EntityId entity = scene.CreateEntity(frame.Position);
                scene.GetRegistry().Components.AddComponent(entity, opening);
                (void)scene.SetParent(entity, wall->Space);
                scene.SetWorldTransform(entity, frame);

                const JsonValue* given = arguments.Find("name");
                const std::string spaceName = EntityNameText(document, wall->Space);
                Rename(document, entity,
                       given != nullptr && given->IsString()
                           ? given->AsString()
                           : scene.NextEntityName(spaceName + (sill > 0.0f ? " window" : " door")));
                recorder.Created(entity);
                document.MarkDirty();
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                    { "opening", Named(document, entity) },
                    { "space", Named(document, wall->Space) },
                }));
            });
        };
        return entry;
    }

    EditorOperationEntry Detach(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "shell.detach";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Space";
        entry.Definition.Description =
            "Turns a space's generated walls, openings already cut, into ordinary brushes under the space, "
            "and stops generating them. The space, its name and its openings stay; the walls can then be "
            "edited by hand. Undo brings the shell back.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "a space's id or '@<name>'"),
        });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return EditorOperationOutcome::Refused(subject.Code, subject.Message);
                EditorScene& scene = document.GetScene();
                const EntityId space = subject.Entities.front();
                World& components = scene.GetRegistry().Components;
                if (!components.HasComponent<Shell>(space))
                    return EditorOperationOutcome::Refused("shell.none", "it has no shell to detach");
                for (const ShellProblem& problem : scene.ShellProblems(space))
                    if (problem.Kind == ShellProblemKind::OpenVolume || problem.Kind == ShellProblemKind::NonPlanarFace
                        || problem.Kind == ShellProblemKind::InvertedSlab)
                        return EditorOperationOutcome::Refused("shell.refused",
                                                               "its shell is refused, so there are no walls: "
                                                                   + problem.Message);

                const BrushEvaluated* evaluated = scene.TryGetBrushPieces(space);
                if (evaluated == nullptr)
                    return EditorOperationOutcome::Refused("shell.none", "it is not a brush");
                std::vector<BrushMesh> walls;
                for (const BrushPiece& piece : evaluated->Pieces)
                    if (evaluated->Meshes[piece.MeshIndex].Role == BrushRole::Solid)
                        walls.push_back(*piece.Mesh);

                const auto* spaceName = components.TryGet<EntityNameComponent>(space);
                const std::string base = (spaceName != nullptr ? std::string(spaceName->Value.View()) : "Space")
                    + " wall";
                recorder.Touch(space);
                components.RemoveComponent<Shell>(space);
                for (BrushMesh& wall : walls)
                {
                    const EntityId brush = scene.CreateBrushFromMesh(Transform3f::Identity(), std::move(wall));
                    (void)scene.SetParent(brush, space);
                    if (auto rename = MakeRenameEntityCommand(brush, scene.NextEntityName(base), scene, document))
                        rename->Execute();
                    recorder.Created(brush);
                }
                document.MarkDirty();
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> SpaceOperations(LevelTarget& level)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Create(level));
    entries.push_back(CreateOpening(level));
    entries.push_back(Detach(level));
    return entries;
}
