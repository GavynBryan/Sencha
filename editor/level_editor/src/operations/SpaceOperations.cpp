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
#include "meshedit/MeshEditService.h"

#include <core/metadata/DataSchemaFields.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

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

    std::optional<Vec3d> Compass(std::string_view name)
    {
        if (name == "north") return Vec3d{ 0, 0, -1 };
        if (name == "south") return Vec3d{ 0, 0, 1 };
        if (name == "east") return Vec3d{ 1, 0, 0 };
        if (name == "west") return Vec3d{ -1, 0, 0 };
        if (name == "up") return Vec3d{ 0, 1, 0 };
        if (name == "down") return Vec3d{ 0, -1, 0 };
        return std::nullopt;
    }

    DataFieldSchema CompassField(std::string key, std::string summary)
    {
        return DataField::Enum(std::move(key), {}, std::move(summary),
                               { { "north", "North", "-Z" }, { "south", "South", "+Z" }, { "east", "East", "+X" },
                                 { "west", "West", "-X" }, { "up", "Up", "+Y" }, { "down", "Down", "-Y" } });
    }

    Vec3d WorldNormal(const BrushMesh& mesh, const BrushFace& face, const Transform3f& world)
    {
        Vec3d normal{ 0, 0, 0 };
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
            normal = normal + world.TransformPoint(mesh.Vertices[face.Loop[i]].Position)
                                  .Cross(world.TransformPoint(mesh.Vertices[face.Loop[(i + 1) % face.Loop.size()]].Position));
        return normal.Normalized();
    }

    // The face of `entity`'s volume whose outward normal is nearest `direction`.
    std::optional<std::uint32_t> FaceToward(const EditorScene& scene, EntityId entity, const Vec3d& direction)
    {
        const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
        const Transform3f* world = scene.TryGetWorldTransform(entity);
        if (mesh == nullptr || world == nullptr)
            return std::nullopt;
        std::optional<std::uint32_t> best;
        float bestDot = 0.7f;
        for (std::uint32_t f = 0; f < mesh->Faces.size(); ++f)
            if (const float dot = WorldNormal(*mesh, mesh->Faces[f], *world).Dot(direction); dot > bestDot)
            {
                best = f;
                bestDot = dot;
            }
        return best;
    }

    // Moves one face of `entity`'s volume along the world `delta`. False when
    // the result would not be a usable brush, or would turn any face inside
    // out.
    bool MoveFace(EditorScene& scene, EntityId entity, std::uint32_t face, const Vec3d& delta)
    {
        const BrushMesh& mesh = *scene.TryGetBrushMesh(entity);
        const Transform3f& world = *scene.TryGetWorldTransform(entity);
        const SelectableRef ref = SelectableRef::FaceSelection(scene.GetRegistry().Id, entity, face);
        std::optional<BrushMesh> moved =
            MeshEditService{}.TranslateElements(mesh, world, std::span(&ref, 1), MeshElementKind::Face, delta, true);
        if (!moved || moved->Faces.size() != mesh.Faces.size())
            return false;
        for (std::size_t f = 0; f < mesh.Faces.size(); ++f)
            if (moved->Faces[f].Normal.Dot(mesh.Faces[f].Normal) <= 0.0f)
                return false;
        scene.SetBrushMesh(entity, std::move(*moved));
        return true;
    }

    // The space on the far side of `opening`: a shelled volume, other than
    // `owner`, with a face looking back along the opening's through axis that
    // the opening's middle lies over, within a wall's thickness and a half.
    struct FarSide
    {
        EntityId      Space;
        std::uint32_t Face = 0;
        Vec3d         Outward;
    };

    std::optional<FarSide> FarSideOf(const EditorScene& scene, EntityId owner, EntityId opening)
    {
        const Transform3f* frame = scene.TryGetWorldTransform(opening);
        const Opening* cut = scene.GetRegistry().Components.TryGet<Opening>(opening);
        if (frame == nullptr || cut == nullptr)
            return std::nullopt;
        const Vec3d through = frame->Rotation.RotateVector(Vec3d{ 0, 0, 1 });
        const Vec3d middle = frame->Position + frame->Rotation.RotateVector(Vec3d{ 0, 0.5f * cut->Height, 0 });
        for (const EntityId entity : scene.GetAllEntities())
        {
            if (entity == owner || scene.RoleOf(entity) != BrushRole::Volume
                || !scene.GetRegistry().Components.HasComponent<Space>(entity))
                continue;
            const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
            const Transform3f* world = scene.TryGetWorldTransform(entity);
            if (mesh == nullptr || world == nullptr)
                continue;
            for (std::uint32_t f = 0; f < mesh->Faces.size(); ++f)
            {
                const Vec3d normal = WorldNormal(*mesh, mesh->Faces[f], *world);
                if (normal.Dot(through) > -0.99f)
                    continue;
                const Vec3d corner = world->TransformPoint(mesh->Vertices[mesh->Faces[f].Loop.front()].Position);
                const float depth = normal.Dot(middle - corner);
                if (depth < -kOpeningReach || depth > kOpeningReach)
                    continue;
                const Vec3d onFace = middle - normal * depth;
                bool inside = true;
                const std::vector<std::uint32_t>& loop = mesh->Faces[f].Loop;
                for (std::size_t i = 0; i < loop.size() && inside; ++i)
                {
                    const Vec3d a = world->TransformPoint(mesh->Vertices[loop[i]].Position);
                    const Vec3d b = world->TransformPoint(mesh->Vertices[loop[(i + 1) % loop.size()]].Position);
                    inside = (b - a).Cross(onFace - a).Dot(normal) >= -1e-3f;
                }
                if (inside)
                    return FarSide{ entity, f, normal };
            }
        }
        return std::nullopt;
    }

    void Translate(EditorScene& scene, EntityId entity, const Vec3d& offset)
    {
        Transform3f world = *scene.TryGetWorldTransform(entity);
        world.Position = world.Position + offset;
        scene.SetWorldTransform(entity, world);
    }

    EditorOperationEntry Move(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "space.move";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Space";
        entry.Definition.Description =
            "Moves a space and what it owns. With carry, the default, each door it owns pulls the face of "
            "the space on the far side by the move's part along that face, so a corridor stretches or "
            "shrinks to stay attached. A sideways move cannot be carried and leaves the door off the "
            "corridor's end; a far side whose walls were detached is skipped and named.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "a space's id or '@<name>'"),
            DataField::Vector("offset", {}, "metres to move by"),
            DataField::Optional("carry", {}, "pull the spaces its doors open onto; true when absent",
                                DataField::Field({}, DataFieldKind::Bool, {}, {})),
        });
        entry.Definition.Result = MutationResultSchema({
            DataField::ArrayOf("carried", {}, "spaces whose face was pulled along", NamedSchema({}, {}), true),
            DataField::ArrayOf("skipped", {}, "spaces left alone because their walls were detached",
                               NamedSchema({}, {}), true),
        });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return EditorOperationOutcome::Refused(subject.Code, subject.Message);
                EditorScene& scene = document.GetScene();
                const EntityId space = subject.Entities.front();
                if (!scene.GetRegistry().Components.HasComponent<Space>(space))
                    return EditorOperationOutcome::Refused("space.not_space", "it is not a space");
                const Vec3d offset = PointFromJson(*arguments.Find("offset"));
                const JsonValue* carryArgument = arguments.Find("carry");
                const bool carry = carryArgument == nullptr || !carryArgument->IsBool() || carryArgument->AsBool();

                // The far sides are found before anything moves, where the doors still are.
                std::vector<FarSide> farSides;
                if (carry)
                    for (const EntityId child : scene.GetAllEntities())
                        if (const Parent* parent = scene.GetRegistry().Components.TryGet<Parent>(child);
                            parent != nullptr && parent->Entity == space)
                            if (const std::optional<FarSide> far = FarSideOf(scene, space, child))
                                if (std::none_of(farSides.begin(), farSides.end(), [&](const FarSide& seen) {
                                        return seen.Space == far->Space && seen.Face == far->Face;
                                    }))
                                    farSides.push_back(*far);

                recorder.Touch(space);
                Translate(scene, space, offset);
                JsonValue::Array carried, skipped;
                for (const FarSide& far : farSides)
                {
                    if (!scene.GetRegistry().Components.HasComponent<Shell>(far.Space))
                    {
                        skipped.push_back(Named(document, far.Space));
                        continue;
                    }
                    const Vec3d pull = far.Outward * offset.Dot(far.Outward);
                    if (pull.Magnitude() < 1e-4f)
                        continue;
                    recorder.Touch(far.Space);
                    if (!MoveFace(scene, far.Space, far.Face, pull))
                        return EditorOperationOutcome::Refused(
                            "space.carry_invalid", "carrying would turn " + EntityNameText(document, far.Space)
                                                       + " inside out; move it less, or without carry");
                    carried.push_back(Named(document, far.Space));
                }
                document.MarkDirty();
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                    { "carried", JsonValue(std::move(carried)) },
                    { "skipped", JsonValue(std::move(skipped)) },
                }));
            });
        };
        return entry;
    }

    EditorOperationEntry ResizeFace(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "space.resize_face";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Space";
        entry.Definition.Description =
            "Pushes the face of a space that looks toward a compass direction outward by a distance, or "
            "pulls it in with a negative one. Its walls follow.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "a space's id or '@<name>'"),
            CompassField("face", "the face looking this way"),
            DataField::Field("distance", DataFieldKind::Float, {}, "metres outward; negative pulls it in"),
        });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return EditorOperationOutcome::Refused(subject.Code, subject.Message);
                EditorScene& scene = document.GetScene();
                const EntityId space = subject.Entities.front();
                if (!scene.GetRegistry().Components.HasComponent<Space>(space))
                    return EditorOperationOutcome::Refused("space.not_space", "it is not a space");
                const Vec3d direction = *Compass(arguments.Find("face")->AsString());
                const std::optional<std::uint32_t> face = FaceToward(scene, space, direction);
                if (!face)
                    return EditorOperationOutcome::Refused("space.no_face", "no face looks that way");
                const BrushMesh& mesh = *scene.TryGetBrushMesh(space);
                const Vec3d normal = WorldNormal(mesh, mesh.Faces[*face], *scene.TryGetWorldTransform(space));
                recorder.Touch(space);
                if (!MoveFace(scene, space, *face, normal * static_cast<float>(arguments.Find("distance")->AsNumber())))
                    return EditorOperationOutcome::Refused("space.resize_invalid",
                                                           "that would turn the space inside out");
                document.MarkDirty();
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }

    EditorOperationEntry MoveOpening(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "opening.move";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Space";
        entry.Definition.Description = "Slides an opening by an offset; the walls it cuts follow it.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "an opening's id or '@<name>'"),
            DataField::Vector("offset", {}, "metres to move by"),
        });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return EditorOperationOutcome::Refused(subject.Code, subject.Message);
                const EntityId opening = subject.Entities.front();
                if (!document.GetScene().GetRegistry().Components.HasComponent<Opening>(opening))
                    return EditorOperationOutcome::Refused("opening.not_opening", "it is not an opening");
                recorder.Touch(opening);
                Translate(document.GetScene(), opening, PointFromJson(*arguments.Find("offset")));
                document.MarkDirty();
                return EditorOperationOutcome::Ok({});
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
    entries.push_back(Move(level));
    entries.push_back(ResizeFace(level));
    entries.push_back(MoveOpening(level));
    entries.push_back(Detach(level));
    return entries;
}
