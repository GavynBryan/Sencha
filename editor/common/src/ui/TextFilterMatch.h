#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

// Case-insensitive substring match for filter boxes. An empty needle matches
// everything, which is what an empty filter field means.
[[nodiscard]] inline bool TextFilterMatch(std::string_view haystack,
                                          std::string_view needle)
{
    if (needle.empty())
        return true;
    const auto it = std::search(
        haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b)
        {
            return std::tolower(static_cast<unsigned char>(a))
                == std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}
