#pragma once

#include <core/metadata/RuntimeSchema.h>
#include <core/serialization/Archive.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

//=============================================================================
// IComponentSerializer
//
// Interface for saving and loading a single component type to/from an archive.
// Each implementation handles one component kind; the scene serializer iterates
// all registered implementations to read/write a full registry.
//=============================================================================
struct IComponentSerializer
{
    // Module-stable runtime identity of the component this serializer handles.
    // Together with JsonKey() and BinaryChunkId() it forms the registration
    // identity tuple the registry validates against aliasing (see §3.3).
    virtual ComponentTypeId TypeId() const = 0;
    virtual std::string_view JsonKey() const = 0;
    virtual std::uint32_t BinaryChunkId() const = 0;

    // Type-erased, flattened leaf-scalar fields of this component, for the
    // editor's registry-driven inspector. The editor reads/writes these at
    // offsets within the component's raw bytes; no ImGui dependency reaches the
    // engine or game modules. (docs/plans/sencha-level-editor/02-...md §5.3.)
    virtual std::span<const RuntimeField> RuntimeFields() const = 0;

    // The bytes of a value-initialized component (honoring the type's C++ default
    // member initializers), for type-erased "add component" — so a new component
    // starts at its intended defaults, not all-zero. Empty for tag components.
    virtual std::vector<std::byte> DefaultBytes() const = 0;

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
