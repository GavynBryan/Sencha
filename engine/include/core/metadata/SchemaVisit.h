#pragma once

#include <tuple>

//=============================================================================
// VisitSchema
//
// Iterates every field declared by Schema and calls visitor.Field(field, member)
// for each one. Visitor must expose a templated Field(const FieldT&, T&) overload.
// Two overloads are provided — const and mutable — so the same visitor type can
// be used for both reading and writing.
//=============================================================================

template <typename Schema, typename T, typename Visitor>
void VisitSchema(const T& value, Visitor& visitor)
{
    auto fields = Schema::Fields();
    std::apply([&](auto&... field)
    {
        (visitor.Field(field, value.*field.Ptr), ...);
    }, fields);
}

template <typename Schema, typename T, typename Visitor>
void VisitSchema(T& value, Visitor& visitor)
{
    auto fields = Schema::Fields();
    std::apply([&](auto&... field)
    {
        (visitor.Field(field, value.*field.Ptr), ...);
    }, fields);
}
