#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

//=============================================================================
// ComponentSerializerRegistry
//
// The engine-owned set of component serializers, as an explicit object rather
// than file-scope global state. A host (app, editor) owns one instance and hands
// it to a loaded game module via GameModuleContext; the module registers its
// component serializers INTO the host's single instance, never its own copy.
// (See docs/plans/sencha-level-editor/02-...md §2.1.)
//
// Registration validates the full (TypeId, JsonKey, BinaryChunkId) identity
// tuple: a full match is idempotent; any partial overlap is a conflict and is
// rejected — two components must never alias one identity facet (01-§3.3).
//=============================================================================
class ComponentSerializerRegistry
{
public:
    enum class RegisterResult
    {
        Added,          // new serializer accepted
        AlreadyPresent, // identical tuple already registered: idempotent no-op
        Rejected        // partial identity overlap with a different component
    };

    RegisterResult Register(std::unique_ptr<IComponentSerializer> serializer)
    {
        if (!serializer)
            return RegisterResult::Rejected;

        for (const auto& existing : Entries_)
        {
            const bool sameType  = existing->TypeId() == serializer->TypeId();
            const bool sameChunk = existing->BinaryChunkId() == serializer->BinaryChunkId();
            const bool sameKey   = existing->JsonKey() == serializer->JsonKey();

            if (sameType && sameChunk && sameKey)
                return RegisterResult::AlreadyPresent;
            if (sameType || sameChunk || sameKey)
                return RegisterResult::Rejected;
        }

        Entries_.push_back(std::move(serializer));
        return RegisterResult::Added;
    }

    void Clear() { Entries_.clear(); }

    // Remove the serializer for a specific component identity. A module calls this
    // in Unregister to retract exactly its own serializers (and free them) while
    // it is still mapped — never Clear(), which would also drop the host's. The
    // unique_ptr is destroyed here, so the owning module must still be loaded.
    bool Remove(ComponentTypeId type)
    {
        for (auto it = Entries_.begin(); it != Entries_.end(); ++it)
        {
            if ((*it)->TypeId() == type)
            {
                Entries_.erase(it);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] const std::vector<std::unique_ptr<IComponentSerializer>>& Entries() const
    {
        return Entries_;
    }

    // unique_ptr::get() is const-qualified and yields a non-const IComponentSerializer*,
    // so a const registry still hands out mutable serializers for Load().
    [[nodiscard]] IComponentSerializer* FindByJsonKey(std::string_view key) const
    {
        for (const auto& entry : Entries_)
            if (key == entry->JsonKey())
                return entry.get();
        return nullptr;
    }

    [[nodiscard]] IComponentSerializer* FindByChunkId(std::uint32_t chunkId) const
    {
        for (const auto& entry : Entries_)
            if (entry->BinaryChunkId() == chunkId)
                return entry.get();
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<IComponentSerializer>> Entries_;
};

// The process-default instance behind the legacy free-function API in
// SceneSerializer.h. New code (the module context) should take an explicit
// ComponentSerializerRegistry& instead of reaching for this.
ComponentSerializerRegistry& DefaultComponentSerializerRegistry();
