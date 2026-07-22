#include <assets/cook/CookFingerprint.h>

#include <core/hash/ContentHash.h>

#include <bit>
#include <cstddef>
#include <span>

namespace
{
    template <typename T>
    std::uint64_t AddScalar(std::uint64_t seed, const T& value)
    {
        return HashBytes64(std::as_bytes(std::span<const T>{ &value, 1 }), seed);
    }

    std::uint64_t AddText(std::uint64_t seed, std::string_view value)
    {
        seed = AddScalar(seed, static_cast<std::uint64_t>(value.size()));
        return HashBytes64(value, seed);
    }
}

CookFingerprint::CookFingerprint(std::string_view mechanism, std::uint32_t version)
{
    Hash = AddText(Hash, mechanism);
    Hash = AddScalar(Hash, version);
}

CookFingerprint& CookFingerprint::AddField(std::string_view field)
{
    Hash = AddText(Hash, field);
    return *this;
}

CookFingerprint& CookFingerprint::AddBool(std::string_view field, bool value)
{
    AddField(field);
    Hash = AddScalar(Hash, static_cast<std::uint8_t>(value ? 1 : 0));
    return *this;
}

CookFingerprint& CookFingerprint::AddU32(std::string_view field, std::uint32_t value)
{
    AddField(field);
    Hash = AddScalar(Hash, value);
    return *this;
}

CookFingerprint& CookFingerprint::AddI32(std::string_view field, std::int32_t value)
{
    AddField(field);
    Hash = AddScalar(Hash, value);
    return *this;
}

CookFingerprint& CookFingerprint::AddU64(std::string_view field, std::uint64_t value)
{
    AddField(field);
    Hash = AddScalar(Hash, value);
    return *this;
}

CookFingerprint& CookFingerprint::AddFloat(std::string_view field, float value)
{
    AddField(field);
    Hash = AddScalar(Hash, std::bit_cast<std::uint32_t>(value));
    return *this;
}

CookFingerprint& CookFingerprint::AddDouble(std::string_view field, double value)
{
    AddField(field);
    Hash = AddScalar(Hash, std::bit_cast<std::uint64_t>(value));
    return *this;
}

CookFingerprint& CookFingerprint::AddString(std::string_view field, std::string_view value)
{
    AddField(field);
    Hash = AddText(Hash, value);
    return *this;
}

CookFingerprint& CookFingerprint::AddDependency(std::string_view step,
                                                std::uint64_t fingerprint)
{
    AddField("dependency");
    Hash = AddText(Hash, step);
    Hash = AddScalar(Hash, fingerprint);
    return *this;
}

