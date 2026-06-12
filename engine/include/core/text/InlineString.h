#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

//=============================================================================
// InlineString<Capacity>
//
// A fixed-capacity, null-terminated, trivially-copyable string. It exists so
// short authored names can live inside ECS components: archetype storage
// relocates components with memcpy, so a component member must be trivially
// copyable — std::string is not.
//
// Construction and assignment from a string_view truncate at Capacity-1 and
// always keep the buffer null-terminated. View() returns the live characters.
// Capacity includes the null terminator.
//=============================================================================
template <std::size_t Capacity>
struct InlineString
{
    static_assert(Capacity >= 1, "InlineString needs room for a null terminator");

    char Data[Capacity] = {};

    InlineString() = default;

    InlineString(std::string_view text)
    {
        Assign(text);
    }

    // A string literal is const char[N]; without this it would need two
    // user-defined conversions (→ string_view → InlineString) and fail.
    InlineString(const char* text)
    {
        Assign(text == nullptr ? std::string_view{} : std::string_view(text));
    }

    InlineString& operator=(std::string_view text)
    {
        Assign(text);
        return *this;
    }

    void Assign(std::string_view text)
    {
        const std::size_t count = std::min(text.size(), Capacity - 1);
        std::memcpy(Data, text.data(), count);
        Data[count] = '\0';
        // Zero the tail so equality and hashing see a canonical buffer.
        if (count + 1 < Capacity)
            std::memset(Data + count + 1, 0, Capacity - count - 1);
    }

    [[nodiscard]] std::string_view View() const
    {
        return std::string_view(Data, Length());
    }

    [[nodiscard]] std::size_t Length() const
    {
        return ::strnlen(Data, Capacity);
    }

    [[nodiscard]] bool Empty() const { return Data[0] == '\0'; }

    [[nodiscard]] bool operator==(const InlineString& other) const
    {
        return View() == other.View();
    }
};
