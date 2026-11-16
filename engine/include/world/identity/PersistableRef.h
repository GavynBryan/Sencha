#pragma once

#include <core/identity/Id.h>
#include <ecs/EntityId.h>
#include <world/serialization/SceneFieldCodec.h>

class World;

// A reference that survives a save, a cook and a restream, as an EntityId does
// not: scenes store the target's persistent id, and the handle it resolved to
// last is found again through the World's PersistentEntityIndex.
struct PersistableRef
{
    EntityId Entity;
    PersistentEntityId Target;

    friend bool operator==(const PersistableRef&, const PersistableRef&) = default;
};

// `entity` with its persistent id; one without an id is held but not persistable.
[[nodiscard]] PersistableRef MakePersistableRef(const World& world, EntityId entity);
// The live entity it names: the handle while it is alive, else whatever entity
// carries its id now. None while the target is not resident.
[[nodiscard]] EntityId ResolvePersistableRef(const World& world, const PersistableRef& ref);
// Keeps the resolved handle, so the next read is a liveness check.
void RefreshPersistableRef(const World& world, PersistableRef& ref);

template <>
struct SceneFieldCodec<PersistableRef>
{
    static bool Save(IWriteArchive&, std::string_view, const PersistableRef&, SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, PersistableRef&, SceneSerializationContext&);
};
