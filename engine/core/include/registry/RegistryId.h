#pragma once

#include <cstdint>

struct RegistryId
{
    uint16_t Index = 0;
    uint16_t Generation = 0;

    static constexpr RegistryId Invalid()
    {
        return { 0, 0 };
    }

    static constexpr RegistryId Global()
    {
        return { 1, 1 };
    }

    bool IsValid() const
    {
        return Index != 0 && Generation != 0;
    }

    bool IsGlobal() const
    {
        return *this == Global();
    }

    bool operator==(const RegistryId&) const = default;
};
