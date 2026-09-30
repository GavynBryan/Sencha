#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h"

#include <cassert>
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
                            EditorScene& scene,
                            EditorDocument& document)
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
        // The snapshot must be the whole component. A shorter one would leave
        // the tail as it stands rather than as it was recorded; a longer one
        // would write past the column. Both mean the snapshot was taken against
        // a different type than the one being written.
        const ComponentMeta* meta = world.GetMeta(Component);
        assert(meta != nullptr && meta->Size == bytes.size()
               && "RawComponentEditCommand: snapshot is not this component's size");
        if (meta == nullptr || meta->Size != bytes.size())
            return;

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
    EditorScene&            Scene;
    EditorDocument&         Document;
};
