#pragma once

#include "../../commands/ICommand.h"
#include "../../brush/BrushMesh.h"

#include <ecs/EntityId.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class EditorDocument;
class EditorScene;
class SelectionService;

// Splits the selected faces out of a brush into a NEW brush entity: the faces
// leave the source (opening it where they were) and the new entity gets them
// under the same LocalTransform (identical local frame, so positions and
// world-aligned texture placement are untouched). Undo destroys the new entity
// and restores the source mesh. Refuses to separate every face (the source
// must keep at least one).
class SeparateFacesCommand : public ICommand
{
public:
    SeparateFacesCommand(EntityId source, std::vector<std::uint32_t> faces,
                         EditorScene& scene, EditorDocument& document, SelectionService& selection);

    void Execute() override;
    void Undo() override;

private:
    EditorScene& Scene;
    EditorDocument& Document;
    SelectionService& Selection;
    EntityId Source;
    EntityId Created;
    std::vector<std::uint32_t> Faces;
    BrushMesh SourceBefore;
    BrushMesh SourceAfter;
    BrushMesh SeparatedMesh;
    bool Captured = false;
};

// nullptr when the faces do not resolve on one brush entity or separating them
// would empty the source.
[[nodiscard]] std::unique_ptr<ICommand> MakeSeparateFacesCommand(
    EntityId source, std::span<const std::uint32_t> faces,
    EditorScene& scene, EditorDocument& document, SelectionService& selection);
