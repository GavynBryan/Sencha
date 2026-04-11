#pragma once

#include <istream>
#include <type_traits>

//=============================================================================
// BinaryReader
//
// Utility for reading binary data from a stream.
// Intended for low-level deserialization of trivial binary values and raw bytes.
// Higher-level formats should build on top of this rather than storing runtime
// object graphs directly.
//=============================================================================
class BinaryReader
{
public:
    explicit BinaryReader(std::istream& stream) : Stream(stream) {}

    template<typename T>
    [[nodiscard]]
    bool Read(T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return ReadBytes(reinterpret_cast<char*>(&value), sizeof(T));
    }

    [[nodiscard]]
    bool ReadBytes(char* buffer, std::streamsize count);

    // Direct stream access for seekable operations (e.g. chunk skipping).
    std::istream& GetStream() { return Stream; }

private:
    std::istream& Stream;
};