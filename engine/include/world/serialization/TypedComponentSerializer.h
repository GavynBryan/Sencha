#pragma once

#include <core/metadata/ComponentPersistence.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <world/serialization/IComponentSerializer.h>

#include <cstring>
#include <vector>

// What every hand-written serializer of T does alike: T's identity, persistence,
// defaults and storage. A serializer derived from this writes only T's persisted form:
// JsonKey, BinaryChunkId, RuntimeFields, Save and LoadIntoWorld.
template <typename T>
struct TypedComponentSerializer : IComponentSerializer
{
    ComponentTypeId TypeId() const final { return ResolveComponentTypeId<T>(); }

    ComponentPersistence Persistence() const final { return ComponentPersistenceOf<T>::Value; }

    std::vector<std::byte> DefaultBytes() const final
    {
        const T value{};
        std::vector<std::byte> bytes(sizeof(T));
        std::memcpy(bytes.data(), &value, sizeof(T));
        return bytes;
    }

    void RegisterStorage(Registry& registry) const final
    {
        if (!registry.Components.IsRegistered<T>())
            registry.Components.RegisterComponent<T>();
    }

    bool HasComponent(EntityId entity, const Registry& registry) const final
    {
        return registry.Components.IsRegistered<T>() && registry.Components.HasComponent<T>(entity);
    }

    bool Load(IReadArchive& archive, EntityId entity, Registry& registry, SceneSerializationContext& context) final
    {
        return LoadIntoWorld(archive, entity, registry.Components, context);
    }

    bool Remove(EntityId entity, Registry& registry) const final
    {
        if (registry.Components.IsRegistered<T>() && registry.Components.HasComponent<T>(entity))
            registry.Components.RemoveComponent<T>(entity);
        return true;
    }

protected:
    // A batch importer may have made the row at its final signature already.
    static bool Place(World& world, EntityId entity, const T& value)
    {
        if (world.HasComponent<T>(entity))
            return world.InitializeComponent<T>(entity, value);
        world.AddComponent<T>(entity, value);
        return true;
    }
};
