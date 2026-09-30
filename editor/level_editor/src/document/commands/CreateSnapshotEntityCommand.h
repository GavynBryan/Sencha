#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h"
#include "document/EntitySnapshot.h"

#include <utility>

class CreateSnapshotEntityCommand final : public ICommand
{
public:
    CreateSnapshotEntityCommand(EditorDocument& document, EntitySnapshot snapshot)
        : Document(document)
        , Snapshot(std::move(snapshot))
    {
    }

    void Execute() override
    {
        Entity = Document.RestoreEntity(Snapshot);
        Document.MarkDirty();
    }

    void Undo() override
    {
        Document.GetScene().DestroyEntity(Entity);
        Document.MarkDirty();
    }

    [[nodiscard]] EntityId CreatedEntity() const { return Entity; }

private:
    EditorDocument& Document;
    EntitySnapshot Snapshot;
    EntityId Entity;
};
