#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

// Fills a fixed ImGui text buffer, truncating to fit and always terminating it.
inline void CopyToTextBuffer(std::string_view value, char* buffer, std::size_t size)
{
    if (size == 0)
        return;
    const std::size_t count = std::min(value.size(), size - 1);
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}
