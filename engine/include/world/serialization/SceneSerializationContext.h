#pragma once

#include <core/identity/Id.h>
#include <ecs/EntityId.h>

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

class AssetSystem;
class LoggingProvider;
class World;

struct PackageMember
{
    PersistentEntityId Authored;
    EntityId Entity;
    PersistentEntityId Carried;
};

// Which form a serializer writes (docs/gameplay/saves.md, "The entity patch").
enum class SceneRecordPurpose : std::uint8_t
{
    Authoring,
    Save,
};

// An entity field a read named by a persistent id that is not resident yet.
struct UnresolvedEntityReference
{
    EntityId* Field = nullptr;
    PersistentEntityId Target;
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
    // Set by the entity record writer for the components it writes.
    SceneRecordPurpose Purpose = SceneRecordPurpose::Authoring;
    // Set by a serializer while it writes or reads a component of this World,
    // for the entity fields it names by persistent id.
    const World* Entities = nullptr;
    std::vector<UnresolvedEntityReference>* Unresolved = nullptr;
};
