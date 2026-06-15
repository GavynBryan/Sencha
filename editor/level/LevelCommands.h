#pragma once

#include "../commands/ICommand.h"
#include "../selection/ISelectionContext.h"

#include "LevelDocument.h"

#include <cstddef>
#include <cstring>
#include <optional>
#include <vector>

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

// Type-erased sibling of EditComponentCommand: edits a single component by its
// raw bytes, without naming its C++ type. The inspector uses this to make ANY
// component undoable — engine or game-module — driven only by the serializer
// registry. Components are trivially copyable (World guarantees memcpy-relocatable
// storage), so before/after byte snapshots are a complete, safe record.
class RawComponentEditCommand : public ICommand
{
public:
    RawComponentEditCommand(EntityId entity,
                            ComponentId componentId,
                            std::vector<std::byte> before,
                            std::vector<std::byte> after,
                            LevelScene& scene,
                            LevelDocument& document)
        : Entity(entity)
        , Component(componentId)
        , Before(std::move(before))
        , After(std::move(after))
        , Scene(scene)
        , Document(document)
    {
    }

    void Execute() override { Apply(After); }
    void Undo() override    { Apply(Before); }

private:
    void Apply(const std::vector<std::byte>& bytes)
    {
        World& world = Scene.GetRegistry().Components;
        if (void* dst = world.GetComponentRaw(Entity, Component))
        {
            std::memcpy(dst, bytes.data(), bytes.size());
            Document.MarkDirty();
        }
    }

    EntityId               Entity;
    ComponentId            Component;
    std::vector<std::byte> Before;
    std::vector<std::byte> After;
    LevelScene&            Scene;
    LevelDocument&         Document;
};

// Type-erased "add component": adds a registered component to an entity by its
// ComponentId, undoably. The initial bytes come from the serializer's
// DefaultBytes() (value-initialized, honoring C++ default member initializers),
// so a new component starts at its intended defaults — not all-zero. Lets the
// inspector offer any registered component — engine or game-module — by identity.
class RawComponentAddCommand : public ICommand
{
public:
    RawComponentAddCommand(EntityId entity, ComponentId componentId,
                           std::vector<std::byte> initialBytes,
                           LevelScene& scene, LevelDocument& document)
        : Entity(entity)
        , Component(componentId)
        , InitialBytes(std::move(initialBytes))
        , Scene(scene)
        , Document(document)
    {
    }

    void Execute() override
    {
        World& world = Scene.GetRegistry().Components;
        const ComponentMeta* meta = world.GetMeta(Component);
        if (meta == nullptr || world.HasComponent(Entity, Component))
            return;
        const void* blob = (meta->Size > 0 && InitialBytes.size() == meta->Size)
            ? InitialBytes.data()
            : nullptr;
        world.AddComponentRaw(Entity, Component, blob, meta->Size, meta->Alignment, nullptr);
        Document.MarkDirty();
    }

    void Undo() override
    {
        World& world = Scene.GetRegistry().Components;
        if (world.HasComponent(Entity, Component))
        {
            world.RemoveComponentRaw(Entity, Component, nullptr);
            Document.MarkDirty();
        }
    }

private:
    EntityId               Entity;
    ComponentId            Component;
    std::vector<std::byte> InitialBytes;
    LevelScene&            Scene;
    LevelDocument&         Document;
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
