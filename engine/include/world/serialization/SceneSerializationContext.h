#pragma once

#include <core/identity/Id.h>
#include <ecs/EntityId.h>

#include <cassert>
#include <span>

class AssetSystem;
class LoggingProvider;

struct PackageMember
{
    PersistentEntityId Authored;
    EntityId Entity;
    PersistentEntityId Carried;
};

//=============================================================================
// SceneSerializationContext
//
// Explicit dependencies used by scene field codecs to convert stable persisted
// references into runtime handles, and runtime handles back into stable refs.
//=============================================================================
struct SceneSerializationContext
{
    SceneSerializationContext(LoggingProvider& logging, AssetSystem* assets = nullptr)
        : Assets(assets)
        , Logging(&logging)
    {
        assert(Logging != nullptr);
    }

    AssetSystem* Assets = nullptr;
    LoggingProvider* Logging = nullptr;
    // Set by the importer for one package, sorted by Authored.
    std::span<const PackageMember> PackageMembers;
};
