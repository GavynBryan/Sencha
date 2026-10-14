#include "SpaceOperations.h"

#include "LevelSubjects.h"
#include "LevelTarget.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "document/SpaceComponents.h"
#include "document/commands/RenameEntityCommand.h"

#include <core/metadata/DataSchemaFields.h>
#include <world/registry/Registry.h>

#include <string>

namespace
{
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
    entries.push_back(Detach(level));
    return entries;
}
