#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

//=============================================================================
// Field
//
// Descriptor for a named member of Class. Carries the member pointer and
// serialization metadata (optional flag, default value) consumed by archives
// and schema visitors. Build instances with MakeField(), then chain Optional()
// or Default() to annotate them.
//
// Usage:
//   MakeField("health", &Actor::Health)
//   MakeField("speed",  &Actor::Speed).Default(1.0f)
//   MakeField("tag",    &Actor::Tag).Optional()
//=============================================================================

template <typename Class, typename Member>
struct Field
{
    std::string_view Name;
    Member Class::* Ptr = nullptr;
    bool IsOptional = false;
    std::optional<Member> DefaultValue;
    std::uint32_t StableId = 0;

    Field& Optional()
    {
        IsOptional = true;
        return *this;
    }

    Field& Default(Member value)
    {
        DefaultValue = value;
        IsOptional = true;
        return *this;
    }
};

template <typename Class, typename Member>
Field<Class, Member> MakeField(std::string_view name, Member Class::* ptr)
{
    return { name, ptr };
}
