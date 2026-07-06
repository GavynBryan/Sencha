#pragma once

#include "commands/ICommand.h"
#include "document/AssetFieldIo.h"
#include "document/EditorDocument.h"

#include <core/assets/AssetRef.h>

#include <cstddef>
#include <utility>

class AssetSystem;

// Edits an asset-handle field (a member tagged AsAsset) by logical reference,
// for any asset type and arity. Unlike RawComponentEditCommand's byte snapshots,
// it routes through AssetFieldIo so the handle refcount stays balanced: each
// apply retains the new asset(s) and releases the one(s) it replaces, so undo
// and redo are correct and re-picks do not leak. The before/after values are
// {id, path} references resolved id-first, so an edit follows a renamed asset.
// The command names no handle type; AssetFieldIo owns that dispatch.
class AssetFieldEditCommand : public ICommand
{
public:
    AssetFieldEditCommand(EntityId entity,
                          ComponentId component,
                          std::size_t offset,
                          AssetType type,
                          AssetArity arity,
                          AssetFieldValue before,
                          AssetFieldValue after,
                          EditorScene& scene,
                          EditorDocument& document,
                          AssetSystem& assets)
        : Entity(entity)
        , Component(component)
        , Offset(offset)
        , Type(type)
        , Arity(arity)
        , Before(std::move(before))
        , After(std::move(after))
        , Scene(scene)
        , Document(document)
        , Assets(assets)
    {
    }

    void Execute() override { Write(After); }
    void Undo() override    { Write(Before); }

private:
    void Write(const AssetFieldValue& value)
    {
        World& world = Scene.GetRegistry().Components;
        void* live = world.GetComponentRaw(Entity, Component);
        if (live == nullptr)
            return;
        ApplyAssetField(Assets, Type, Arity,
                        static_cast<std::byte*>(live) + Offset, value);
        Document.MarkDirty();
    }

    EntityId        Entity;
    ComponentId     Component;
    std::size_t     Offset;
    AssetType       Type;
    AssetArity      Arity;
    AssetFieldValue Before;
    AssetFieldValue After;
    EditorScene&     Scene;
    EditorDocument&  Document;
    AssetSystem&    Assets;
};
