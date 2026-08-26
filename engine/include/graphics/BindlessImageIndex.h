#pragma once

#include <cstdint>

// Slot in the bindless sampled-image array (descriptor set 1). Shaders index
// the array with this value; UINT32_MAX is the unregistered state.
//
// Declared apart from the descriptor cache so types that merely *hold* a
// bindless index -- texture caches, lightmap components -- do not pull the
// Vulkan headers in behind it: the graphics/BufferHandle.h precedent.
struct BindlessImageIndex
{
    std::uint32_t Value = UINT32_MAX;

    [[nodiscard]] bool IsValid() const { return Value != UINT32_MAX; }
    bool operator==(const BindlessImageIndex&) const = default;
};
