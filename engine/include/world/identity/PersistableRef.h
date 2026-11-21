#pragma once

#include <core/identity/Id.h>
#include <core/metadata/DataSchema.h>
#include <ecs/EntityId.h>
#include <world/serialization/SceneFieldCodec.h>

class World;

struct PersistableRef
{
    EntityId Entity;
    PersistentEntityId Target;

    friend bool operator==(const PersistableRef&, const PersistableRef&) = default;
};

// Mints the entity an identity on the authority when it has none.
[[nodiscard]] PersistableRef MakePersistableRef(World& world, EntityId entity);
// Never mints: an entity without an identity gives a reference no save can follow.
[[nodiscard]] PersistableRef PersistableRefOf(const World& world, EntityId entity);
[[nodiscard]] EntityId ResolvePersistableRef(const World& world, const PersistableRef& ref);
void RefreshPersistableRef(const World& world, PersistableRef& ref);

template <>
struct SceneFieldCodec<PersistableRef>
{
    // A placement renames the reference when it names one of its own members.
    static constexpr DataFieldKind CompositionKind = DataFieldKind::Entity;
    static bool Save(IWriteArchive&, std::string_view, const PersistableRef&, SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, PersistableRef&, SceneSerializationContext&);
};
