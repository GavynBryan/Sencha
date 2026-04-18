#pragma once

#include <ostream>
#include <type_traits>

//=============================================================================
// BinaryWriter
//
// Utility for writing binary data to a stream.
// Intended for low-level serialization of trivial binary values and raw bytes.
// Higher-level formats should build on top of this rather than storing runtime
// object graphs directly.
//=============================================================================
class BinaryWriter
{
public:
    explicit BinaryWriter(std::ostream& stream) : Stream(stream) {}

    template<typename T>
    [[nodiscard]]
    bool Write(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return WriteBytes(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    [[nodiscard]]
    bool WriteBytes(const char* buffer, std::streamsize count);

    // Direct stream access for seekable operations (e.g. chunk size patching).
    std::ostream& GetStream() { return Stream; }

private:
    std::ostream& Stream;
};