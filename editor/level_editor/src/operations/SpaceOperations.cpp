#include "SpaceOperations.h"

#include "DiagnosticRecords.h"
#include "EntityRecords.h"
#include "LevelJson.h"
#include "LevelNavigation.h"
#include "LevelSubjects.h"
#include "LevelTarget.h"
#include "SpaceDiagnostics.h"
#include "SpaceEdits.h"
#include "SpaceProposals.h"
#include "SpaceRelations.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "document/LevelConventions.h"
#include "document/NavigationCookInput.h"
#include "document/SpaceComponents.h"
#include "document/SpaceWalls.h"
#include "document/commands/RenameEntityCommand.h"

#include <core/metadata/DataSchemaFields.h>
#include <navigation/tooling/NavMeshSnapshot.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace
{
    constexpr float kSmallestOpening = 0.1f;

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
        entry.Definition.Result = MutationResultSchema({ EntityNameSchema("space", "the space made") });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                NewSpace wanted;
                wanted.Name = arguments.Find("name")->AsString();
                wanted.Min = PointFromJson(*arguments.Find("min"));
                wanted.Max = PointFromJson(*arguments.Find("max"));
                const JsonValue* shell = arguments.Find("shell");
                wanted.Shelled = shell == nullptr || !shell->IsBool() || shell->AsBool();
                wanted.Thickness = NumberOf(arguments, "thickness");
                EntityId space;
                if (EditorOperationOutcome refused = CreateSpace(document, recorder, wanted, space);
                    refused.Status != EditorOperationStatus::Ok)
                    return refused;
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "space", EntityNameJson(document, space) } }));
            });
        };
        return entry;
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
            EntityNameSchema("opening", "the opening made"),
            EntityNameSchema("space", "the space whose wall it cuts and that owns it"),
        });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                EditorScene& scene = document.GetScene();
                const JsonValue* given = arguments.Find("name");
                const std::string chosen = given != nullptr && given->IsString() ? NormalizedEntityName(given->AsString())
                                                                                 : std::string{};
                if (std::optional<EditorOperationOutcome> refused = RefuseEntityName(document, EntityId{}, false, chosen))
                    return std::move(*refused);
                const std::optional<WallHit> wall = WallAt(scene, PointFromJson(*arguments.Find("at")));
                if (!wall)
                    return EditorOperationOutcome::Refused("opening.no_wall",
                                                           "no generated wall of a space is at that point");
                const LevelConventions conventions = ConventionsOf(document);
                Opening opening;
                opening.Width = NumberOf(arguments, "width").value_or(conventions.OpeningWidth);
                opening.Height = NumberOf(arguments, "height").value_or(conventions.OpeningHeight);
                if (opening.Width < kSmallestOpening || opening.Height < kSmallestOpening)
                    return EditorOperationOutcome::Refused("opening.too_small", "width and height must be at least 0.1 m");
                const JsonValue* shape = arguments.Find("shape");
                opening.Shape = shape != nullptr && shape->IsString() && shape->AsString() == "arch" ? CarveShape::Arch
                                                                                                      : CarveShape::Rectangle;
                const float sill = NumberOf(arguments, "sill").value_or(0.0f);

                const Vec3d up{ 0, 1, 0 };
                Transform3f frame;
                frame.Position = Vec3d{ wall->OnWall.X, wall->FloorY + sill, wall->OnWall.Z };
                frame.Rotation = Quatf::FromBasis(up.Cross(wall->Outward), up, wall->Outward);
                recorder.Read(wall->Space);
                const EntityId entity = scene.CreateEntity(frame.Position);
                scene.GetRegistry().Components.AddComponent(entity, opening);
                (void)scene.SetParent(entity, wall->Space);
                scene.SetWorldTransform(entity, frame);

                const std::string spaceName = EntityNameText(document, wall->Space);
                Rename(document, entity,
                       !chosen.empty() ? chosen : scene.NextEntityName(spaceName + (sill > 0.0f ? " window" : " door")));
                recorder.Created(entity);
                document.MarkDirty();
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                    { "opening", EntityNameJson(document, entity) },
                    { "space", EntityNameJson(document, wall->Space) },
                }));
            });
        };
        return entry;
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
            DataField::ArrayOf("carried", {}, "spaces whose face was pulled along", EntityNameSchema({}, {}), true),
            DataField::ArrayOf("skipped", {}, "spaces left alone because their walls were detached",
                               EntityNameSchema({}, {}), true),
        });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return SubjectRefusal(subject);
                EditorScene& scene = document.GetScene();
                const EntityId space = subject.Entities.front();
                if (!scene.GetRegistry().Components.HasComponent<Space>(space))
                    return EditorOperationOutcome::Refused("space.not_space", "it is not a space");
                const JsonValue* carryArgument = arguments.Find("carry");
                const bool carry = carryArgument == nullptr || !carryArgument->IsBool() || carryArgument->AsBool();
                Transform3f moved = *scene.TryGetWorldTransform(space);
                moved.Position = moved.Position + PointFromJson(*arguments.Find("offset"));
                const EntityMove move{ space, moved };
                MoveReport report;
                if (EditorOperationOutcome refused = MoveEntities(document, recorder, std::span(&move, 1), carry, report);
                    refused.Status != EditorOperationStatus::Ok)
                    return refused;
                JsonValue::Array carried, skipped;
                for (const EntityId entity : report.Carried)
                    carried.push_back(EntityNameJson(document, entity));
                for (const EntityId entity : report.Skipped)
                    skipped.push_back(EntityNameJson(document, entity));
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
            AxisDirectionField("face", "the face looking this way"),
            DataField::Field("distance", DataFieldKind::Float, {}, "metres outward; negative pulls it in"),
        });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return SubjectRefusal(subject);
                EditorScene& scene = document.GetScene();
                const EntityId space = subject.Entities.front();
                if (!scene.GetRegistry().Components.HasComponent<Space>(space))
                    return EditorOperationOutcome::Refused("space.not_space", "it is not a space");
                const Vec3d direction = *AxisDirection(arguments.Find("face")->AsString());
                const std::optional<std::uint32_t> face = FaceToward(scene, space, direction);
                if (!face)
                    return EditorOperationOutcome::Refused("space.no_face", "no face looks that way");
                const Vec3d normal = WorldFaceOf(scene, space, *face)->Normal;
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
                    return SubjectRefusal(subject);
                const EntityId opening = subject.Entities.front();
                if (!document.GetScene().GetRegistry().Components.HasComponent<Opening>(opening))
                    return EditorOperationOutcome::Refused("opening.not_opening", "it is not an opening");
                if (const std::optional<EntityId> owner = OwnerOf(document.GetScene(), opening))
                    recorder.Read(*owner);
                Transform3f moved = *document.GetScene().TryGetWorldTransform(opening);
                moved.Position = moved.Position + PointFromJson(*arguments.Find("offset"));
                const EntityMove move{ opening, moved };
                MoveReport unused;
                return MoveEntities(document, recorder, std::span(&move, 1), false, unused);
            });
        };
        return entry;
    }

    EditorOperationEntry Diagnostics(const LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "level.diagnostics";
        entry.Definition.Category = "Space";
        entry.Definition.Description =
            "What is wrong with the level's spaces and openings: refused or approximate shells, spaces "
            "that overlap, openings that could not be cut, lead into no space, or are too small for the "
            "navigation profile's capsule. Each record is about an entity, by persistent id.";
        entry.Definition.Result = DataField::Record({}, {}, {}, { ContentRiskRecordsSchema("records") });
        entry.Execute = [&level](const JsonValue&) {
            const EditorDocument& document = level.Document();
            std::optional<OpeningClearance> clearance;
            if (!document.ContentRoots().empty())
                if (const NavigationSettingsLookup settings = FindNavigationSettings(document.ContentRoots().front());
                    settings.Settings && !settings.Settings->Profiles.empty())
                    clearance = OpeningClearance{ settings.Settings->Profiles.front().Build.Radius,
                                                  settings.Settings->Profiles.front().Build.Height };
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "records", ContentRiskRecordsJson(SpaceDiagnostics(document.GetScene(), clearance)) },
            }));
        };
        return entry;
    }

    // Appends `entity` to `list` once, by id and name.
    void Note(JsonValue::Array& list, const EditorDocument& document, EntityId entity)
    {
        JsonValue named = EntityNameJson(document, entity);
        if (std::none_of(list.begin(), list.end(), [&](const JsonValue& seen) {
                return seen.Find("id")->AsString() == named.Find("id")->AsString();
            }))
            list.push_back(std::move(named));
    }

    void Note(JsonValue::Array& list, const EditorDocument& document, PersistentEntityId id)
    {
        const SubjectResolution found = ResolveEntitySubject(document, PersistentEntityIdToString(id));
        if (found.Resolved())
            Note(list, document, found.Entities.front());
    }

    EditorOperationEntry Deps(const LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "deps";
        entry.Definition.Category = "Space";
        entry.Definition.Description =
            "What follows from an entity and would change with it. For a space: how many wall brushes it "
            "built, the openings cutting them, the spaces it shares a wall with, and the spaces its "
            "doors lead into. For an opening: the spaces whose walls it cuts and the spaces it joins.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "an entity id or '@<name>'"),
        });
        entry.Definition.Result = DataField::Record({}, {}, {}, {
            EntityNameSchema("subject", {}),
            DataField::Field("walls", DataFieldKind::Int, {}, "the wall brushes it built"),
            DataField::ArrayOf("cut_by", {}, "openings cutting its walls", EntityNameSchema({}, {}), true),
            DataField::ArrayOf("shares_walls_with", {}, {}, EntityNameSchema({}, {}), true),
            DataField::ArrayOf("leads_to", {}, "spaces its openings lead into", EntityNameSchema({}, {}), true),
            DataField::ArrayOf("cuts", {}, "spaces whose walls this opening cuts", EntityNameSchema({}, {}), true),
            DataField::ArrayOf("joins", {}, "the spaces either side of this opening", EntityNameSchema({}, {}), true),
        });
        entry.Execute = [&level](const JsonValue& arguments) {
            const EditorDocument& document = level.Document();
            const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
            if (!subject.Resolved())
                return SubjectRefusal(subject);
            const EditorScene& scene = document.GetScene();
            const World& components = scene.GetRegistry().Components;
            const EntityId entity = subject.Entities.front();

            std::int64_t walls = 0;
            JsonValue::Array cutBy, sharesWith, leadsTo, cuts, joins;
            const std::optional<SpaceShell> shell = BuildSpaceShell(scene, entity);
            const std::vector<PersistentEntityId> cutting = shell ? shell->Openings : std::vector<PersistentEntityId>{};
            if (shell)
            {
                walls = static_cast<std::int64_t>(SpaceWalls(scene, entity).size());
                for (const PersistentEntityId id : shell->Openings)
                    Note(cutBy, document, id);
                for (const PersistentEntityId id : shell->SharedWith)
                    Note(sharesWith, document, id);
            }
            // Every opening in its walls or of its own leads to whichever side is not it.
            for (const EntityId other : scene.GetAllEntities())
            {
                if (!components.HasComponent<Opening>(other))
                    continue;
                const std::optional<EntityId> owner = OwnerOf(scene, other);
                const bool inItsWalls = std::ranges::find(cutting, scene.PersistentIdOf(other)) != cutting.end();
                if (!owner || (*owner != entity && !inItsWalls))
                    continue;
                const std::optional<FarSide> far = FarSideOf(scene, *owner, other);
                if (*owner != entity)
                    Note(leadsTo, document, *owner);
                else if (far)
                    Note(leadsTo, document, far->Space);
            }

            if (components.HasComponent<Opening>(entity))
            {
                const PersistentEntityId self = scene.PersistentIdOf(entity);
                for (const EntityId other : scene.GetAllEntities())
                    if (const std::optional<SpaceShell> walled = BuildSpaceShell(scene, other);
                        walled && std::ranges::find(walled->Openings, self) != walled->Openings.end())
                        Note(cuts, document, other);
                if (const std::optional<EntityId> owner = OwnerOf(scene, entity))
                {
                    Note(joins, document, *owner);
                    if (const std::optional<FarSide> far = FarSideOf(scene, *owner, entity))
                        Note(joins, document, far->Space);
                }
            }

            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "subject", EntityNameJson(document, entity) },
                { "walls", JsonValue(static_cast<double>(walls)) },
                { "cut_by", JsonValue(std::move(cutBy)) },
                { "shares_walls_with", JsonValue(std::move(sharesWith)) },
                { "leads_to", JsonValue(std::move(leadsTo)) },
                { "cuts", JsonValue(std::move(cuts)) },
                { "joins", JsonValue(std::move(joins)) },
            }));
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
            "Stops a space building its walls: its wall brushes stay as they stand, openings already cut, "
            "as ordinary brushes under the space. The space, its name and its openings stay. Undo brings the "
            "shell back.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "a space's id or '@<name>'"),
        });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return SubjectRefusal(subject);
                EditorScene& scene = document.GetScene();
                const EntityId space = subject.Entities.front();
                World& components = scene.GetRegistry().Components;
                const std::optional<SpaceShell> shell = BuildSpaceShell(scene, space);
                if (!shell)
                    return EditorOperationOutcome::Refused("shell.none", "it has no shell to detach");
                for (const ShellProblem& problem : shell->Result.Problems)
                    if (problem.Kind == ShellProblemKind::OpenVolume || problem.Kind == ShellProblemKind::NonPlanarFace
                        || problem.Kind == ShellProblemKind::InvertedSlab)
                        return EditorOperationOutcome::Refused("shell.refused",
                                                               "its shell is refused, so there are no walls: "
                                                                   + problem.Message);
                recorder.Touch(space);
                components.RemoveComponent<Shell>(space);
                for (const EntityId wall : SpaceWalls(scene, space))
                {
                    recorder.Touch(wall);
                    components.RemoveComponent<GeneratedWall>(wall);
                }
                document.MarkDirty();
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }

    EditorOperationEntry Rebuild(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "shell.rebuild";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Space";
        entry.Definition.Description =
            "Rebuilds a space's walls from its volume, taking back the wall brushes edited by hand and kept, "
            "as one undoable step. Paint on the walls stays.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "a space's id or '@<name>'"),
        });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return SubjectRefusal(subject);
                const EntityId space = subject.Entities.front();
                if (!BuildSpaceShell(document.GetScene(), space))
                    return EditorOperationOutcome::Refused("shell.none", "it has no shell to rebuild");
                RebuildSpaceWalls(document, recorder, space, true);
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }
}

namespace
{
    EditorOperationEntry Propose(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "space.propose";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Space";
        entry.Definition.Description =
            "Proposes spaces for walkable surface no space holds yet, read off the level's navigation: a room "
            "wherever the floor runs at least core_depth from any edge, a passage for the walkable surface "
            "left between. A heuristic: run it in a draft and check the plan before accepting. The spaces are "
            "made without generated walls, since the level already has its own.";
        DataFieldSchema depth = DataField::Field("core_depth", DataFieldKind::Float, {},
                                                 "metres from the edge of walkable surface a room's middle reaches", false);
        depth.Default = 1.0;
        depth.Numeric.Minimum = 0.25;
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            std::move(depth),
            DataField::Optional("height", {}, "metres from floor to ceiling; the project's space height when absent",
                                DataField::Field({}, DataFieldKind::Float, {}, {})),
            DataField::Optional("profile", {}, "navigation profile; the first one when absent",
                                DataField::Field({}, DataFieldKind::String, {}, {})),
        });
        entry.Definition.Result = MutationResultSchema({
            DataField::ArrayOf("proposed", {}, {},
                DataField::Record({}, {}, {}, {
                    EntityNameSchema("space", {}),
                    DataField::Enum("kind", {}, {}, { { "room", "Room", {} }, { "passage", "Passage", {} } }),
                    DataField::Field("area", DataFieldKind::Float, {}, "square metres of walkable surface"),
                }),
                true),
        });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                EditorOperationOutcome refusal;
                const std::unique_ptr<LevelNavigation> navigation = BuildLevelNavigation(document, arguments, refusal);
                if (navigation == nullptr)
                    return refusal;
                const NavMeshSnapshot snapshot = SnapshotNavMesh(navigation->Navigation, navigation->ProfileIndex);
                EditorScene& scene = document.GetScene();
                std::vector<std::uint32_t> unheld;
                for (std::uint32_t index = 0; index < snapshot.Polygons.size(); ++index)
                {
                    const NavSnapshotPolygon& polygon = snapshot.Polygons[index];
                    Vec3d centre = Vec3d::Zero();
                    for (std::uint32_t v = 0; v < polygon.VertexCount; ++v)
                        centre = centre + snapshot.Vertices[polygon.FirstVertex + v];
                    centre = centre * (1.0f / static_cast<float>(polygon.VertexCount));
                    if (!SpaceContaining(scene, centre + Vec3d{ 0, 0.1f, 0 }))
                        unheld.push_back(index);
                }

                SpaceProposalSettings settings;
                settings.CoreDepth = static_cast<float>(arguments.Find("core_depth")->AsNumber());
                settings.Inset = navigation->File.Profiles[navigation->ProfileIndex].Build.Radius;
                const LevelConventions conventions = ConventionsOf(document);
                const float height = NumberOf(arguments, "height").value_or(conventions.SpaceHeight);

                JsonValue::Array proposed;
                for (const ProposedSpace& space : ProposeSpaces(snapshot, unheld, settings))
                {
                    NewSpace wanted;
                    wanted.Name = scene.NextEntityName(space.Kind == ProposedSpaceKind::Room ? "Room" : "Passage");
                    wanted.Min = space.Min;
                    wanted.Max = Vec3d{ space.Max.X, space.Min.Y + height, space.Max.Z };
                    wanted.Shelled = false;
                    EntityId made;
                    if (EditorOperationOutcome refused = CreateSpace(document, recorder, wanted, made);
                        refused.Status != EditorOperationStatus::Ok)
                        return refused;
                    proposed.push_back(JsonValue(JsonValue::Object{
                        { "space", EntityNameJson(document, made) },
                        { "kind", JsonValue(space.Kind == ProposedSpaceKind::Room ? "room" : "passage") },
                        { "area", MetresJson(space.Area) },
                    }));
                }
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "proposed", JsonValue(std::move(proposed)) } }));
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
    entries.push_back(Rebuild(level));
    entries.push_back(Diagnostics(level));
    entries.push_back(Deps(level));
    entries.push_back(Propose(level));
    return entries;
}
