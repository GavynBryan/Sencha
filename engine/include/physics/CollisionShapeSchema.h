#pragma once

#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <math/MathSchemas.h>
#include <physics/CollisionShape.h>

#include <array>
#include <string_view>
#include <tuple>

template <>
struct EnumSchema<CollisionShapeType>
{
    static constexpr std::array Values = {
        EnumValue{ CollisionShapeType::Sphere, "sphere" },
        EnumValue{ CollisionShapeType::Box, "box" },
        EnumValue{ CollisionShapeType::Capsule, "capsule" },
    };
};

template <>
struct TypeSchema<CollisionShape>
{
    static constexpr std::string_view Name = "CollisionShape";

    static auto Fields()
    {
        const CollisionShape defaults;
        return std::tuple{
            MakeField("type", &CollisionShape::Type).Default(defaults.Type),
            MakeField("half_extents", &CollisionShape::HalfExtents).Default(defaults.HalfExtents),
            MakeField("radius", &CollisionShape::Radius).Default(defaults.Radius),
            MakeField("half_height", &CollisionShape::HalfHeight).Default(defaults.HalfHeight),
            MakeField("center", &CollisionShape::Center).Default(defaults.Center),
            MakeField("orientation", &CollisionShape::Orientation).Default(defaults.Orientation),
        };
    }
};
