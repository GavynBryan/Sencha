#pragma once

#include <core/metadata/ComponentRemovable.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// LocalTransform
//
// Authoritative local-space transform for an entity. This is the transform that
// scene serialization writes and gameplay systems edit directly.
//=============================================================================
struct LocalTransform
{
    Transform3f Value;
};

//=============================================================================
// WorldTransform
//
// Derived world-space transform for an entity. Transform propagation owns this
// component; it is reconstructed from LocalTransform and Parent after load.
//=============================================================================
struct WorldTransform
{
    Transform3f Value;
};

//=============================================================================
// Parent
//
// Optional hierarchy component. An entity has a spatial parent iff this
// component is present.
//=============================================================================
struct Parent
{
    EntityId Entity;
};

template <>
struct TypeSchema<LocalTransform>
{
    static constexpr std::string_view Name = "Transform";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('X', 'F', 'R', 'M');

    static auto Fields()
    {
        return std::tuple{
            MakeField("local", &LocalTransform::Value),
        };
    }
};

// Structural: paired with the derived WorldTransform, so the editor must not let
// it be removed (that would orphan the pairing). A transform-less entity is made
// by never adding one, not by stripping it. (core/metadata/ComponentRemovable.h)
template <>
struct ComponentRemovable<LocalTransform>
{
    static constexpr bool Value = false;
};

// WorldTransform and Parent are pure-runtime (never serialized themselves), so
// they carry no TypeSchema. They still need module-stable identity for the World
// type→id map — declared explicitly here. (LocalTransform's identity derives from
// its TypeSchema::Name above.)
SENCHA_DECLARE_COMPONENT_TYPE(WorldTransform, "sencha.world_transform");
SENCHA_DECLARE_COMPONENT_TYPE(Parent,         "sencha.parent");
