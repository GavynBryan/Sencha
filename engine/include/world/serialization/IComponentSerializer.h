#pragma once

#include <core/serialization/Archive.h>
#include <ecs/EntityId.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <cstdint>
#include <string_view>

//=============================================================================
// IComponentSerializer
//
// Interface for saving and loading a single component type to/from an archive.
// Each implementation handles one component kind; the scene serializer iterates
// all registered implementations to read/write a full registry.
//=============================================================================
struct IComponentSerializer
{
    virtual std::string_view JsonKey() const = 0;
    virtual std::uint32_t BinaryChunkId() const = 0;

    virtual void RegisterStorage(Registry& registry) const = 0;
    virtual bool HasComponent(EntityId entity, const Registry& registry) const = 0;
    virtual bool Save(IWriteArchive& archive,
                      EntityId entity,
                      const Registry& registry,
                      SceneSerializationContext& context) const = 0;
    virtual bool Load(IReadArchive& archive,
                      EntityId entity,
                      Registry& registry,
                      SceneSerializationContext& context) = 0;
    virtual bool Remove(EntityId entity, Registry& registry) const = 0;

    virtual ~IComponentSerializer() = default;
};
