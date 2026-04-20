#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/JsonShape.h>
#include <core/metadata/TypeSchema.h>
#include <math/Quat.h>
#include <math/Vec.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>

#include <string_view>
#include <tuple>

template <int N, typename T>
struct JsonShapeFor<Vec<N, T>>
{
    static constexpr JsonShape Value = JsonShape::Array;
};

template <typename T>
struct JsonShapeFor<Quat<T>>
{
    static constexpr JsonShape Value = JsonShape::Array;
};

template <typename T>
struct TypeSchema<Vec<2, T>>
{
    static constexpr std::string_view Name = "Vec2";

    static auto Fields()
    {
        return std::tuple{
            MakeField("x", &Vec<2, T>::X),
            MakeField("y", &Vec<2, T>::Y),
        };
    }
};

template <typename T>
struct TypeSchema<Vec<3, T>>
{
    static constexpr std::string_view Name = "Vec3";

    static auto Fields()
    {
        return std::tuple{
            MakeField("x", &Vec<3, T>::X),
            MakeField("y", &Vec<3, T>::Y),
            MakeField("z", &Vec<3, T>::Z),
        };
    }
};

template <typename T>
struct TypeSchema<Vec<4, T>>
{
    static constexpr std::string_view Name = "Vec4";

    static auto Fields()
    {
        return std::tuple{
            MakeField("x", &Vec<4, T>::X),
            MakeField("y", &Vec<4, T>::Y),
            MakeField("z", &Vec<4, T>::Z),
            MakeField("w", &Vec<4, T>::W),
        };
    }
};

template <typename T>
struct TypeSchema<Quat<T>>
{
    static constexpr std::string_view Name = "Quat";

    static auto Fields()
    {
        return std::tuple{
            MakeField("x", &Quat<T>::X),
            MakeField("y", &Quat<T>::Y),
            MakeField("z", &Quat<T>::Z),
            MakeField("w", &Quat<T>::W),
        };
    }
};

template <typename T>
struct TypeSchema<Transform3d<T>>
{
    static constexpr std::string_view Name = "Transform3d";

    static auto Fields()
    {
        return std::tuple{
            MakeField("position", &Transform3d<T>::Position),
            MakeField("rotation", &Transform3d<T>::Rotation),
            MakeField("scale", &Transform3d<T>::Scale),
        };
    }
};

template <typename T>
struct TypeSchema<Transform2d<T>>
{
    static constexpr std::string_view Name = "Transform2d";

    static auto Fields()
    {
        return std::tuple{
            MakeField("position", &Transform2d<T>::Position),
            MakeField("rotation", &Transform2d<T>::Rotation),
            MakeField("scale", &Transform2d<T>::Scale),
        };
    }
};
