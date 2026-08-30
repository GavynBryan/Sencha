#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h"

#include <cstddef>
#include <utility>
#include <vector>

// Adds a registered component to an entity by its ComponentId, undoably. The
// initial bytes come from the serializer's DefaultBytes() (value-initialized,
// honoring C++ default member initializers), so a new component starts at its
// intended defaults — not all-zero. Lets the inspector offer any registered
// component — engine or game-module — by identity.
//
// Adding one component can put several on the entity: a component declares what
// it cannot work without and the World provisions that set. So the undo is a
// diff rather than a single removal — what the add actually brought, which is
// not something this command could re-derive without a second copy of the rule.
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

        // Recomputed on every Execute, so a redo takes back exactly what that
        // redo brought rather than what the first add did.
        world.ComponentIdsOn(Entity, Before);

        const void* blob = (meta->Size > 0 && InitialBytes.size() == meta->Size)
            ? InitialBytes.data()
            : nullptr;
        world.AddComponentRaw(Entity, Component, blob, meta->Size, meta->Alignment, nullptr);

        world.ComponentIdsOn(Entity, After);
        Provisioned.clear();
        for (const ComponentId id : After)
            if (id != Component && !Carried(Before, id))
                Provisioned.push_back(id);

        Document.MarkDirty();
    }

    void Undo() override
    {
        World& world = Scene.GetRegistry().Components;
        // Reverse order, and the named component last: a provisioned component
        // may hold something its own OnRemove has to release, and none of them
        // should outlive the component that asked for it.
        for (std::size_t i = Provisioned.size(); i-- > 0;)
            Remove(world, Provisioned[i]);
        Provisioned.clear();
        Remove(world, Component);
        Document.MarkDirty();
    }

private:
    static bool Carried(const std::vector<ComponentId>& ids, ComponentId id)
    {
        for (const ComponentId carried : ids)
            if (carried == id)
                return true;
        return false;
    }

    // Through the component's own OnRemove hook. Undo now takes back
    // components this command did not name, and one of them may hold an asset
    // reference; a raw removal that skipped the hook would strand it.
    void Remove(World& world, ComponentId id)
    {
        if (!world.HasComponent(Entity, id))
            return;
        const ComponentMeta* meta = world.GetMeta(id);
        world.RemoveComponentRaw(Entity, id,
                                 meta != nullptr ? meta->OnRemoveHook : nullptr);
    }

    EntityId                 Entity;
    ComponentId              Component;
    std::vector<std::byte>   InitialBytes;
    std::vector<ComponentId> Provisioned;
    std::vector<ComponentId> Before;
    std::vector<ComponentId> After;
    EditorScene&             Scene;
    EditorDocument&          Document;
};
