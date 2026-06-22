#pragma once

#include "../../commands/ICommand.h"
#include "../LevelDocument.h"

#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

// Edits a single component by its raw bytes, without naming its C++ type. The
// inspector uses this to make ANY component undoable — engine or game-module —
// driven only by the serializer registry. Components are trivially copyable
// (World guarantees memcpy-relocatable storage), so before/after byte snapshots
// are a complete, safe record.
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
