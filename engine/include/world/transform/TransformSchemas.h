#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <math/MathSchemas.h>
#include <world/transform/TransformStore.h>

#include <string_view>
#include <tuple>

template <>
struct TypeSchema<TransformComponent<Transform3f>>
{
    static constexpr std::string_view Name = "Transform";

    static auto Fields()
    {
        return std::tuple{
            MakeField("local", &TransformComponent<Transform3f>::Local),
        };
    }
};
