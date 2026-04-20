#include <core/serialization/BinaryArchive.h>

#include <limits>
#include <string>

IWriteArchive& BinaryWriteArchive::Field(std::string_view, bool value)
{
    IsOk = IsOk && Writer.Write(value);
    return *this;
}

IWriteArchive& BinaryWriteArchive::Field(std::string_view, float value)
{
    IsOk = IsOk && Writer.Write(value);
    return *this;
}

IWriteArchive& BinaryWriteArchive::Field(std::string_view, double value)
{
    IsOk = IsOk && Writer.Write(value);
    return *this;
}

IWriteArchive& BinaryWriteArchive::Field(std::string_view, std::uint32_t value)
{
    IsOk = IsOk && Writer.Write(value);
    return *this;
}

IWriteArchive& BinaryWriteArchive::Field(std::string_view, std::string_view value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
    {
        IsOk = false;
        return *this;
    }

    const auto size = static_cast<std::uint32_t>(value.size());
    IsOk = IsOk && Writer.Write(size);
    if (size > 0)
        IsOk = IsOk && Writer.WriteBytes(value.data(), static_cast<std::streamsize>(size));
    return *this;
}

IWriteArchive& BinaryWriteArchive::BeginObject(std::string_view)
{
    return *this;
}

IWriteArchive& BinaryWriteArchive::BeginArray(std::string_view, std::size_t)
{
    return *this;
}

IWriteArchive& BinaryWriteArchive::End()
{
    return *this;
}

void BinaryWriteArchive::MarkInvalidField(std::string_view)
{
    IsOk = false;
}

IReadArchive& BinaryReadArchive::Field(std::string_view, bool& value)
{
    IsOk = IsOk && Reader.Read(value);
    return *this;
}

IReadArchive& BinaryReadArchive::Field(std::string_view, float& value)
{
    IsOk = IsOk && Reader.Read(value);
    return *this;
}

IReadArchive& BinaryReadArchive::Field(std::string_view, double& value)
{
    IsOk = IsOk && Reader.Read(value);
    return *this;
}

IReadArchive& BinaryReadArchive::Field(std::string_view, std::uint32_t& value)
{
    IsOk = IsOk && Reader.Read(value);
    return *this;
}

IReadArchive& BinaryReadArchive::Field(std::string_view, std::string& value)
{
    std::uint32_t size = 0;
    IsOk = IsOk && Reader.Read(size);
    if (!IsOk)
        return *this;

    value.resize(size);
    if (size > 0)
        IsOk = IsOk && Reader.ReadBytes(value.data(), static_cast<std::streamsize>(size));
    return *this;
}

IReadArchive& BinaryReadArchive::BeginObject(std::string_view)
{
    return *this;
}

IReadArchive& BinaryReadArchive::BeginArray(std::string_view, std::size_t& count)
{
    // Binary schemas carry fixed-size arrays without storing their element count.
    // Generic schema readers ignore this value for binary archives.
    count = 0;
    return *this;
}

IReadArchive& BinaryReadArchive::End()
{
    return *this;
}

bool BinaryReadArchive::HasField(std::string_view) const
{
    return true;
}

bool BinaryReadArchive::IsString(std::string_view) const
{
    return false;
}

bool BinaryReadArchive::IsObject(std::string_view) const
{
    return false;
}

void BinaryReadArchive::MarkMissingField(std::string_view)
{
    IsOk = false;
}

void BinaryReadArchive::MarkInvalidField(std::string_view)
{
    IsOk = false;
}
