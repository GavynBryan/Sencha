#pragma once

#include "../commands/ICommand.h"
#include "../selection/ISelectionContext.h"

#include "LevelDocument.h"

#include <optional>

class CreateBrushCommand : public ICommand
{
public:
    CreateBrushCommand(Vec3d position, Vec3d halfExtents, LevelScene& scene, LevelDocument& document);

    void Execute() override;
    void Undo() override;

    [[nodiscard]] EntityId GetCreatedEntity() const { return CreatedEntity; }

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

class EditBrushCommand : public ICommand
{
public:
    EditBrushCommand(EntityId entity,
                     Vec3d oldPosition, Vec3d newPosition,
                     Vec3d oldHalfExtents, Vec3d newHalfExtents,
                     LevelScene& scene, LevelDocument& document);

    void Execute() override;
    void Undo() override;

private:
    EntityId Entity;
    Vec3d OldPosition;
    Vec3d NewPosition;
    Vec3d OldHalfExtents;
    Vec3d NewHalfExtents;
    LevelScene& Scene;
    LevelDocument& Document;
};

// Replaces a single component's value wholesale. Used by the inspector for
// schema-driven field edits; old/new snapshots make the edit undoable.
template <typename Component>
class EditComponentCommand : public ICommand
{
public:
    EditComponentCommand(EntityId entity, Component oldValue, Component newValue,
                         LevelScene& scene, LevelDocument& document)
        : Entity(entity)
        , OldValue(std::move(oldValue))
        , NewValue(std::move(newValue))
        , Scene(scene)
        , Document(document)
    {
    }

    void Execute() override
    {
        Scene.SetComponent(Entity, NewValue);
        Document.MarkDirty();
    }

    void Undo() override
    {
        Scene.SetComponent(Entity, OldValue);
        Document.MarkDirty();
    }

private:
    EntityId Entity;
    Component OldValue;
    Component NewValue;
    LevelScene& Scene;
    LevelDocument& Document;
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
    std::optional<BrushComponent> SavedBrush;
    std::optional<CameraComponent> SavedCamera;
    EntityId RestoredEntity = {};
    bool CapturedState = false;
};
