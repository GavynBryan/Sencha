#include "SeparateFacesCommand.h"

#include "../EditorDocument.h"
#include "../EditorScene.h"
#include "../../brush/BrushValidation.h"
#include "../../selection/SelectionService.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace
{
// The chosen faces as their own mesh with compacted vertices; source keeps the
// rest. Both sides then run repair (the source may open; authoring tolerates it).
void SplitFaces(const BrushMesh& mesh, std::span<const std::uint32_t> faces,
                BrushMesh& outKept, BrushMesh& outSeparated)
{
    std::unordered_map<std::uint32_t, std::uint32_t> remap;
    for (std::uint32_t faceIndex : faces)
    {
        BrushFace face = mesh.Faces[faceIndex];
        for (std::uint32_t& index : face.Loop)
        {
            const auto [it, inserted] = remap.try_emplace(
                index, static_cast<std::uint32_t>(outSeparated.Vertices.size()));
            if (inserted)
                outSeparated.Vertices.push_back(mesh.Vertices[index]);
            index = it->second;
        }
        outSeparated.Faces.push_back(std::move(face));
    }

    outKept = mesh;
    // Erase in descending index order so earlier indices stay valid.
    std::vector<std::uint32_t> sorted(faces.begin(), faces.end());
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    for (std::uint32_t faceIndex : sorted)
        outKept.Faces.erase(outKept.Faces.begin() + faceIndex);
}
}

SeparateFacesCommand::SeparateFacesCommand(EntityId source, std::vector<std::uint32_t> faces,
                                           EditorScene& scene, EditorDocument& document,
                                           SelectionService& selection)
    : Scene(scene)
    , Document(document)
    , Selection(selection)
    , Source(source)
    , Faces(std::move(faces))
{
}

void SeparateFacesCommand::Execute()
{
    if (!Captured)
    {
        SourceBefore = *Scene.TryGetBrushMesh(Source);
        SplitFaces(SourceBefore, Faces, SourceAfter, SeparatedMesh);
        BrushValidateAndRepair(SourceAfter);
        BrushValidateAndRepair(SeparatedMesh);
        Captured = true;
    }

    Scene.SetBrushMesh(Source, SourceAfter);
    Created = Scene.CreateBrushFromMesh(*Scene.TryGetTransform(Source), SeparatedMesh);

    Selection.SetSelection({ SelectableRef::EntitySelection(Scene.GetRegistry().Id, Created) });
    Document.MarkDirty();
}

void SeparateFacesCommand::Undo()
{
    Scene.DestroyEntity(Created);
    Created = {};
    Scene.SetBrushMesh(Source, SourceBefore);
    Selection.SetSelection({ SelectableRef::EntitySelection(Scene.GetRegistry().Id, Source) });
    Document.MarkDirty();
}

std::unique_ptr<ICommand> MakeSeparateFacesCommand(EntityId source,
                                                   std::span<const std::uint32_t> faces,
                                                   EditorScene& scene, EditorDocument& document,
                                                   SelectionService& selection)
{
    const BrushMesh* mesh = scene.TryGetBrushMesh(source);
    if (mesh == nullptr || scene.TryGetTransform(source) == nullptr || faces.empty())
        return nullptr;

    // Dedup + bounds-check; refuse to empty the source.
    std::vector<std::uint32_t> unique;
    for (std::uint32_t face : faces)
        if (face < mesh->Faces.size()
            && std::find(unique.begin(), unique.end(), face) == unique.end())
            unique.push_back(face);
    if (unique.empty() || unique.size() >= mesh->Faces.size())
        return nullptr;

    return std::make_unique<SeparateFacesCommand>(source, std::move(unique), scene, document, selection);
}
