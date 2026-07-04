#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h" // EditorScene + EditorDocument

#include <ecs/EntityId.h>

#include <functional>
#include <memory>
#include <utility>

//=============================================================================
// CreateEntityCommand — creates an entity via a bound factory on Execute
// (capturing the new id so a follow-up like select-on-create can reference it);
// Undo destroys it. Replaces the near-identical per-type create commands.
//=============================================================================

class CreateEntityCommand : public ICommand
{
public:
    CreateEntityCommand(std::function<EntityId()> create, EditorScene& scene, EditorDocument& document)
        : Create(std::move(create))
        , Scene(scene)
        , Document(document)
    {
    }

    void Execute() override
    {
        CreatedEntity = Create();
        Document.MarkDirty();
    }

    void Undo() override
    {
        Scene.DestroyEntity(CreatedEntity);
        Document.MarkDirty();
    }

    [[nodiscard]] EntityId GetCreatedEntity() const { return CreatedEntity; }

private:
    std::function<EntityId()> Create;
    EditorScene&               Scene;
    EditorDocument&            Document;
    EntityId                  CreatedEntity = {};
};

[[nodiscard]] inline std::unique_ptr<CreateEntityCommand> MakeCreateBrushCommand(
    Vec3d position, Vec3d halfExtents, EditorScene& scene, EditorDocument& document)
{
    return std::make_unique<CreateEntityCommand>(
        [&scene, position, halfExtents] { return scene.CreateBrush(position, halfExtents); }, scene, document);
}

[[nodiscard]] inline std::unique_ptr<CreateEntityCommand> MakeCreateBrushMeshCommand(
    Transform3f transform, BrushMesh mesh, EditorScene& scene, EditorDocument& document)
{
    return std::make_unique<CreateEntityCommand>(
        [&scene, transform, mesh = std::move(mesh)]
        { return scene.CreateBrushFromMesh(transform, mesh); }, scene, document);
}

[[nodiscard]] inline std::unique_ptr<CreateEntityCommand> MakeCreateEntityCommand(
    Vec3d position, EditorScene& scene, EditorDocument& document)
{
    return std::make_unique<CreateEntityCommand>(
        [&scene, position] { return scene.CreateEntity(position); }, scene, document);
}

// A thin box brush already flagged as a portal, so create-and-flag is one undo
// step (Undo's DestroyEntity releases the mesh and the component together).
[[nodiscard]] inline std::unique_ptr<CreateEntityCommand> MakeCreatePortalBrushCommand(
    Vec3d center, Vec3d halfExtents, EditorScene& scene, EditorDocument& document)
{
    return std::make_unique<CreateEntityCommand>(
        [&scene, center, halfExtents]
        {
            const EntityId entity = scene.CreateBrush(center, halfExtents);
            scene.GetRegistry().Components.AddComponent(entity, PortalComponent{});
            return entity;
        },
        scene, document);
}
