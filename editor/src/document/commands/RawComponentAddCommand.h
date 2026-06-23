#pragma once

#include "../../commands/ICommand.h"
#include "../EditorDocument.h"

#include <cstddef>
#include <utility>
#include <vector>

// Adds a registered component to an entity by its ComponentId, undoably. The
// initial bytes come from the serializer's DefaultBytes() (value-initialized,
// honoring C++ default member initializers), so a new component starts at its
// intended defaults — not all-zero. Lets the inspector offer any registered
// component — engine or game-module — by identity.
class RawComponentAddCommand : public ICommand
{
public:
    RawComponentAddCommand(EntityId entity, ComponentId componentId,
                           std::vector<std::byte> initialBytes,
                           EditorScene& scene, EditorDocument& document)
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
    EditorScene&            Scene;
    EditorDocument&         Document;
};
