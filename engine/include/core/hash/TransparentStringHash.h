#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

// Lets a string-keyed unordered container be searched with a string_view
// without building a std::string key; pair it with std::equal_to<>.
struct TransparentStringHash
{
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept
    {
        return std::hash<std::string_view>{}(text);
    }
};
