#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>

#include <cstdint>
#include <string_view>
#include <tuple>

struct StaticMeshHandle
{
    uint32_t Index = 0;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != 0 && Generation != 0; }
    [[nodiscard]] bool IsNull() const { return !IsValid(); }
    [[nodiscard]] uint32_t SlotIndex() const { return Index; }

    bool operator==(const StaticMeshHandle&) const = default;
};

template <>
struct TypeSchema<StaticMeshHandle>
{
    static constexpr std::string_view Name = "StaticMeshHandle";

    static auto Fields()
    {
        return std::tuple{
            MakeField("index", &StaticMeshHandle::Index),
            MakeField("generation", &StaticMeshHandle::Generation),
        };
    }
};
