#pragma once

#include "../../commands/ICommand.h"
#include "../EditorDocument.h" // EditorScene + EditorDocument

#include <ecs/EntityId.h>

#include <functional>
#include <memory>
#include <utility>

//=============================================================================
// ValueCommand<T> — a generic "set a value, undoably" command. Stores the
// before/after value and a bound setter; Execute applies After + marks the doc
// dirty, Undo restores Before. The setter captures the scene + how to write T,
// so the command is value-only. Replaces the per-type before/after boilerplate
// commands (transform move, brush-mesh edit, ...).
//=============================================================================

template <typename T>
class ValueCommand : public ICommand
{
public:
    ValueCommand(T before, T after, std::function<void(const T&)> apply, EditorDocument& document)
        : Before(std::move(before))
        , After(std::move(after))
        , Apply(std::move(apply))
        , Document(document)
    {
    }

    void Execute() override
    {
        Apply(After);
        Document.MarkDirty();
    }

    void Undo() override
    {
        Apply(Before);
        Document.MarkDirty();
    }

private:
    T                             Before;
    T                             After;
    std::function<void(const T&)> Apply;
    EditorDocument&                Document;
};

// Moves an entity by changing only its transform (brush moves go through this, not
// a mesh edit, so translating a brush can't rebuild/clobber its mesh).
[[nodiscard]] inline std::unique_ptr<ICommand> MakeMoveCommand(
    EntityId entity, Transform3f before, Transform3f after, EditorScene& scene, EditorDocument& document)
{
    return std::make_unique<ValueCommand<Transform3f>>(
        before, after, [&scene, entity](const Transform3f& t) { scene.SetTransform(entity, t); }, document);
}

// Replaces a brush's mesh wholesale (the general wrapper for every mesh-edit verb).
[[nodiscard]] inline std::unique_ptr<ICommand> MakeEditBrushMeshCommand(
    EntityId entity, BrushMesh before, BrushMesh after, EditorScene& scene, EditorDocument& document)
{
    return std::make_unique<ValueCommand<BrushMesh>>(
        std::move(before), std::move(after),
        [&scene, entity](const BrushMesh& mesh) { scene.SetBrushMesh(entity, mesh); }, document);
}
