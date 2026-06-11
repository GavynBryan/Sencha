#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

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

    static auto Fields()
    {
        return std::tuple{
            MakeField("local", &LocalTransform::Value),
        };
    }
};
