#include <serialization/Serialize.h>

bool Serialize(BinaryWriter& writer, const std::string& value)
{
    auto length = static_cast<std::uint32_t>(value.size());
    if (!writer.Write(length)) return false;
    if (length == 0) return true;
    return writer.WriteBytes(value.data(), static_cast<std::streamsize>(length));
}

bool Deserialize(BinaryReader& reader, std::string& value, std::uint32_t maxLength)
{
    std::uint32_t length = 0;
    if (!reader.Read(length)) return false;
    if (length > maxLength) return false;
    value.resize(length);
    if (length == 0) return true;
    return reader.ReadBytes(value.data(), static_cast<std::streamsize>(length));
}
