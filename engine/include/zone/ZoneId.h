#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

struct ZoneId
{
    uint32_t Value = 0;

    static constexpr ZoneId Invalid()
    {
        return { 0 };
    }

    bool IsValid() const
    {
        return Value != 0;
    }

    bool operator==(const ZoneId&) const = default;
};

template<>
struct std::hash<ZoneId>
{
    size_t operator()(const ZoneId& id) const noexcept
    {
        return std::hash<uint32_t>{}(id.Value);
    }
};
