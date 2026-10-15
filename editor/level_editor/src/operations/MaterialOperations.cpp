#include "MaterialOperations.h"

#include "LevelJson.h"
#include "LevelSubjects.h"
#include "LevelTarget.h"
#include "SpaceRelations.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "project/ProjectFiles.h"

#include <core/metadata/DataSchemaFields.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace
{
    EditorOperationEntry Apply(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "material.apply";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Material";
        entry.Definition.Description =
            "Paints brushes with a material, every face or only the face toward a direction, as one undoable "
            "step. A space's faces are the inner faces of its walls.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::ArrayOf("subjects", {}, "the brushes to paint",
                               DataField::Field({}, DataFieldKind::String, {}, "an entity id or '@<name>'"), true),
            DataField::Field("material", DataFieldKind::String, {}, "a material's asset path, as assets.list finds it"),
            AxisDirectionField("face", "only the face looking this way; every face when absent"),
        });
        entry.Definition.Parameters.Children.back().Required = false;
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            const std::string& path = arguments.Find("material")->AsString();
            const std::vector<std::filesystem::path>& roots = level.Document().ContentRoots();
            const std::optional<ProjectFile> file = FindProjectFile(roots, path);
            if (!file || file->Extension != ".smat")
                return EditorOperationOutcome::Refused("material.unknown",
                                                       "the project has no material '" + path + "'; see assets.list");
            const AssetRef material = CurrentInProject(roots, AssetRef{ AssetType::Material, path, AssetIdOf(*file) });
            return level.Mutate(name, arguments, [&](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subjects = ResolveEntitySubjects(document, *arguments.Find("subjects"));
                if (!subjects.Resolved())
                    return SubjectRefusal(subjects);
                const JsonValue* face = arguments.Find("face");
                EditorScene& scene = document.GetScene();
                for (const EntityId brush : subjects.Entities)
                {
                    const BrushMesh* mesh = scene.TryGetBrushMesh(brush);
                    if (mesh == nullptr)
                        return EditorOperationOutcome::Refused("material.not_brush",
                                                               EntityNameText(document, brush) + " has no faces to paint");
                    BrushMesh painted = *mesh;
                    if (face != nullptr)
                    {
                        const std::optional<std::uint32_t> toward = FaceToward(scene, brush, *AxisDirection(face->AsString()));
                        if (!toward)
                            return EditorOperationOutcome::Refused("brush.no_face",
                                                                   EntityNameText(document, brush) + " has no face looking that way");
                        painted.Faces[*toward].Material.Material = material;
                    }
                    else
                        for (BrushFace& each : painted.Faces)
                            each.Material.Material = material;
                    recorder.Touch(brush);
                    scene.SetBrushMesh(brush, std::move(painted));
                }
                document.MarkDirty();
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> MaterialOperations(LevelTarget& level)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Apply(level));
    return entries;
}
