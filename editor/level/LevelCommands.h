#pragma once

#include "../commands/ICommand.h"
#include "../selection/ISelectionContext.h"

#include "LevelDocument.h"

#include <optional>

class CreateCubeCommand : public ICommand
{
public:
    CreateCubeCommand(Vec3d position, Vec3d halfExtents, LevelScene& scene, LevelDocument& document);

    void Execute() override;
    void Undo() override;

private:
    Vec3d Position;
    Vec3d HalfExtents;
    LevelScene& Scene;
    LevelDocument& Document;
    EntityId CreatedEntity = {};
};

class CreateCameraCommand : public ICommand
{
public:
    CreateCameraCommand(Vec3d position, LevelScene& scene, LevelDocument& document);

    void Execute() override;
    void Undo() override;

private:
    Vec3d Position;
    LevelScene& Scene;
    LevelDocument& Document;
    EntityId CreatedEntity = {};
};

class DeleteEntityCommand : public ICommand
{
public:
    DeleteEntityCommand(EntityId entity, LevelScene& scene, LevelDocument& document);

    void Execute() override;
    void Undo() override;

private:
    EntityId TargetEntity = {};
    LevelScene& Scene;
    LevelDocument& Document;
    std::optional<Transform3f> SavedTransform;
    std::optional<CubePrimitive> SavedCube;
    std::optional<CameraComponent> SavedCamera;
    EntityId RestoredEntity = {};
    bool CapturedState = false;
};
