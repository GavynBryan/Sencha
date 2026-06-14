#include <core/hash/ContentHash.h>

#include <bit>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint64_t Prime1 = 0x9E3779B185EBCA87ULL;
    constexpr uint64_t Prime2 = 0xC2B2AE3D27D4EB4FULL;
    constexpr uint64_t Prime3 = 0x165667B19E3779F9ULL;
    constexpr uint64_t Prime4 = 0x85EBCA77C2B2AE63ULL;
    constexpr uint64_t Prime5 = 0x27D4EB2F165667C5ULL;

    uint64_t Read64(const std::byte* p)
    {
        uint64_t value;
        std::memcpy(&value, p, sizeof(value));
        return value;
    }

    uint32_t Read32(const std::byte* p)
    {
        uint32_t value;
        std::memcpy(&value, p, sizeof(value));
        return value;
    }

    uint64_t Round(uint64_t acc, uint64_t input)
    {
        acc += input * Prime2;
        acc = std::rotl(acc, 31);
        acc *= Prime1;
        return acc;
    }

    uint64_t MergeRound(uint64_t acc, uint64_t lane)
    {
        acc ^= Round(0, lane);
        return acc * Prime1 + Prime4;
    }
} // namespace

uint64_t HashBytes64(std::span<const std::byte> bytes, uint64_t seed)
{
    const std::byte* p = bytes.data();
    const std::byte* const end = p + bytes.size();

    uint64_t hash;
    if (bytes.size() >= 32)
    {
        uint64_t v1 = seed + Prime1 + Prime2;
        uint64_t v2 = seed + Prime2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - Prime1;

        do
        {
            v1 = Round(v1, Read64(p)); p += 8;
            v2 = Round(v2, Read64(p)); p += 8;
            v3 = Round(v3, Read64(p)); p += 8;
            v4 = Round(v4, Read64(p)); p += 8;
        } while (end - p >= 32);

        hash = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
        hash = MergeRound(hash, v1);
        hash = MergeRound(hash, v2);
        hash = MergeRound(hash, v3);
        hash = MergeRound(hash, v4);
    }
    else
    {
        hash = seed + Prime5;
    }

    hash += static_cast<uint64_t>(bytes.size());

    while (end - p >= 8)
    {
        hash ^= Round(0, Read64(p));
        hash = std::rotl(hash, 27) * Prime1 + Prime4;
        p += 8;
    }

    if (end - p >= 4)
    {
        hash ^= static_cast<uint64_t>(Read32(p)) * Prime1;
        hash = std::rotl(hash, 23) * Prime2 + Prime3;
        p += 4;
    }

    while (p < end)
    {
        hash ^= std::to_integer<uint64_t>(*p) * Prime5;
        hash = std::rotl(hash, 11) * Prime1;
        ++p;
    }

    hash ^= hash >> 33;
    hash *= Prime2;
    hash ^= hash >> 29;
    hash *= Prime3;
    hash ^= hash >> 32;
    return hash;
}

uint64_t HashBytes64(std::string_view text, uint64_t seed)
{
    return HashBytes64(std::as_bytes(std::span(text.data(), text.size())), seed);
}

bool HashFileContents(std::string_view filePath, uint64_t& outHash)
{
    std::ifstream file{ std::string(filePath), std::ios::binary };
    if (!file.is_open())
        return false;

    std::vector<std::byte> bytes;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0)
        return false;
    file.seekg(0, std::ios::beg);

    bytes.resize(static_cast<std::size_t>(size));
    if (size > 0)
        file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file.good() && size > 0)
        return false;

    outHash = HashBytes64(bytes);
    return true;
}
