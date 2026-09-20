#include <logic/VerbRelay.h>

#include <core/serialization/Archive.h>

#include <charconv>
#include <format>
#include <string>

std::string VerbBindingKeyToString(VerbBindingKey key)
{
    return std::format("{:016x}", key.Value);
}

bool VerbBindingKeyFromString(std::string_view text, VerbBindingKey& out)
{
    if (text.size() != 16)
        return false;
    for (const char c : text)
    {
        const bool digit = c >= '0' && c <= '9';
        const bool lowerHex = c >= 'a' && c <= 'f';
        if (!digit && !lowerHex)
            return false;
    }
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0)
        return false;
    out = VerbBindingKey{ value };
    return true;
}

bool SceneFieldCodec<VerbBindingKey>::Save(IWriteArchive& archive, std::string_view key,
                                           VerbBindingKey value, SceneSerializationContext&)
{
    archive.Field(key, std::string_view(VerbBindingKeyToString(value)));
    return archive.Ok();
}

bool SceneFieldCodec<VerbBindingKey>::Load(IReadArchive& archive, std::string_view key,
                                           VerbBindingKey& value, SceneSerializationContext&)
{
    std::string text;
    archive.Field(key, text);
    if (!archive.Ok())
        return false;
    if (!VerbBindingKeyFromString(text, value))
    {
        archive.MarkInvalidField(key);
        return false;
    }
    return true;
}
