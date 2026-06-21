#pragma once

#include "../../commands/ICommand.h"
#include "../LevelDocument.h" // LevelScene + LevelDocument

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
    CreateEntityCommand(std::function<EntityId()> create, LevelScene& scene, LevelDocument& document)
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
    LevelScene&               Scene;
    LevelDocument&            Document;
    EntityId                  CreatedEntity = {};
};

[[nodiscard]] inline std::unique_ptr<CreateEntityCommand> MakeCreateBrushCommand(
    Vec3d position, Vec3d halfExtents, LevelScene& scene, LevelDocument& document)
{
    return std::make_unique<CreateEntityCommand>(
        [&scene, position, halfExtents] { return scene.CreateBrush(position, halfExtents); }, scene, document);
}

[[nodiscard]] inline std::unique_ptr<CreateEntityCommand> MakeCreateCameraCommand(
    Vec3d position, LevelScene& scene, LevelDocument& document)
{
    return std::make_unique<CreateEntityCommand>(
        [&scene, position] { return scene.CreateCamera(position); }, scene, document);
}

[[nodiscard]] inline std::unique_ptr<CreateEntityCommand> MakeCreateEntityCommand(
    Vec3d position, LevelScene& scene, LevelDocument& document)
{
    return std::make_unique<CreateEntityCommand>(
        [&scene, position] { return scene.CreateEntity(position); }, scene, document);
}
