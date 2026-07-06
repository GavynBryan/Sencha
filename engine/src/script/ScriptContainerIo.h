#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// Bounds-checked little-endian cursors shared by the .tbc parser and writer.
// memcpy-based because section payloads are only 4-byte aligned relative to
// the 8-byte section start once counts and records interleave.

namespace ScriptContainerIo
{
    struct Reader
    {
        std::span<const std::byte> Bytes;
        size_t Offset = 0;
        bool Failed = false;

        template <typename T>
        [[nodiscard]] T Read()
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (Failed || Offset + sizeof(T) > Bytes.size())
            {
                Failed = true;
                return T{};
            }
            T value{};
            std::memcpy(&value, Bytes.data() + Offset, sizeof(T));
            Offset += sizeof(T);
            return value;
        }

        [[nodiscard]] bool ReadInto(void* dst, size_t size)
        {
            if (Failed || Offset + size > Bytes.size())
            {
                Failed = true;
                return false;
            }
            std::memcpy(dst, Bytes.data() + Offset, size);
            Offset += size;
            return true;
        }

        [[nodiscard]] bool AtEnd() const { return !Failed && Offset == Bytes.size(); }
    };

    struct Writer
    {
        std::vector<std::byte> Bytes;

        template <typename T>
        void Write(T value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const size_t at = Bytes.size();
            Bytes.resize(at + sizeof(T));
            std::memcpy(Bytes.data() + at, &value, sizeof(T));
        }

        void WriteBytes(const void* src, size_t size)
        {
            const size_t at = Bytes.size();
            Bytes.resize(at + size);
            std::memcpy(Bytes.data() + at, src, size);
        }
    };
}
