#pragma once

enum class JsonShape
{
    Object,
    Array
};

//=============================================================================
// JsonShapeFor
//
// Controls whether a schema-bearing type serializes as a JSON object or a
// JSON array. Specialize for types (e.g. Vec, Quat) where a flat array is a
// more natural representation. Defaults to Object. Binary archives ignore
// this trait entirely.
//
// Usage:
//   template <int N, typename T>
//   struct JsonShapeFor<Vec<N, T>>
//   {
//       static constexpr JsonShape Value = JsonShape::Array;
//   };
//=============================================================================

template <typename T>
struct JsonShapeFor
{
    static constexpr JsonShape Value = JsonShape::Object;
};
