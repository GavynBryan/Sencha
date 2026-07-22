#pragma once

#include <cstdint>
#include <string_view>

// Builds a deterministic cook identity from explicitly named scalar fields.
// Length-prefixing strings and tagging fields prevents distinct input shapes
// from collapsing to the same byte concatenation. Call order is significant.
class CookFingerprint
{
public:
    explicit CookFingerprint(std::string_view mechanism, std::uint32_t version = 1);

    CookFingerprint& AddBool(std::string_view field, bool value);
    CookFingerprint& AddU32(std::string_view field, std::uint32_t value);
    CookFingerprint& AddI32(std::string_view field, std::int32_t value);
    CookFingerprint& AddU64(std::string_view field, std::uint64_t value);
    CookFingerprint& AddFloat(std::string_view field, float value);
    CookFingerprint& AddDouble(std::string_view field, double value);
    CookFingerprint& AddString(std::string_view field, std::string_view value);
    CookFingerprint& AddDependency(std::string_view step, std::uint64_t fingerprint);

    [[nodiscard]] std::uint64_t Value() const { return Hash; }

private:
    CookFingerprint& AddField(std::string_view field);

    std::uint64_t Hash = 0;
};

