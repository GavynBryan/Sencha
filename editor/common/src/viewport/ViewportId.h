#pragma once

#include <cstdint>

struct ViewportId
{
    uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    friend bool operator==(ViewportId, ViewportId) = default;
};
