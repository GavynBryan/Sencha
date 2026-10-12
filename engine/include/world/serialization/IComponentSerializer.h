#pragma once

#include <core/metadata/ComponentPlacement.h>
#include <core/metadata/EditorVisual.h>
#include <core/metadata/RuntimeSchema.h>
#include <core/serialization/Archive.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

//=============================================================================
// IComponentSerializer
//
// Interface for saving and loading a single component type. Editor and legacy
// document paths may still target Registry; runtime zone import targets the one
// unified World directly.
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

    // Runtime import path. The destination World already carries the complete
    // sealed component vocabulary; serializers only decode and insert values.
    virtual bool LoadIntoWorld(IReadArchive& archive,
                               EntityId entity,
                               World& world,
                               SceneSerializationContext& context) = 0;

    virtual bool Remove(EntityId entity, Registry& registry) const = 0;

    // Optional hint for how the editor should visualize an entity carrying this
    // component (e.g. a camera mesh); pure tooling metadata the runtime ignores.
    virtual std::optional<EditorVisual> GetEditorVisual() const { return std::nullopt; }

    // Whether a tool may remove this component from an entity; structural
    // components (the transform) opt out. Pure tooling metadata.
    virtual bool IsRemovable() const { return true; }

    // What a tool may place an entity carrying just this component as; an
    // empty label when it is not a thing of its own. Pure tooling metadata.
    virtual ComponentPlacementFacts Placement() const { return {}; }

    // Last, so every virtual added above moves its slots: an ABI break.
    virtual ~IComponentSerializer() = default;
};
