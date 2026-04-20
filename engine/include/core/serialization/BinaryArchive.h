#pragma once

#include <core/serialization/Archive.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>

//=============================================================================
// BinaryWriteArchive
//
// IWriteArchive backed by a BinaryWriter. Keys are ignored; fields are written
// in declaration order. BeginObject/BeginArray/End are no-ops.
//=============================================================================
class BinaryWriteArchive final : public IWriteArchive
{
public:
    explicit BinaryWriteArchive(BinaryWriter& writer) : Writer(writer) {}

    IWriteArchive& Field(std::string_view key, bool value) override;
    IWriteArchive& Field(std::string_view key, float value) override;
    IWriteArchive& Field(std::string_view key, double value) override;
    IWriteArchive& Field(std::string_view key, std::uint32_t value) override;
    IWriteArchive& Field(std::string_view key, std::string_view value) override;
    IWriteArchive& BeginObject(std::string_view key) override;
    IWriteArchive& BeginArray(std::string_view key, std::size_t count) override;
    IWriteArchive& End() override;
    bool Ok() const override { return IsOk; }
    bool IsText() const override { return false; }

private:
    BinaryWriter& Writer;
    bool IsOk = true;
};

//=============================================================================
// BinaryReadArchive
//
// IReadArchive backed by a BinaryReader. HasField always returns true (fields
// are positional). BeginObject/BeginArray/End are no-ops; BeginArray returns
// count=0 because element count is not stored in the binary format.
//=============================================================================
class BinaryReadArchive final : public IReadArchive
{
public:
    explicit BinaryReadArchive(BinaryReader& reader) : Reader(reader) {}

    IReadArchive& Field(std::string_view key, bool& value) override;
    IReadArchive& Field(std::string_view key, float& value) override;
    IReadArchive& Field(std::string_view key, double& value) override;
    IReadArchive& Field(std::string_view key, std::uint32_t& value) override;
    IReadArchive& Field(std::string_view key, std::string& value) override;
    IReadArchive& BeginObject(std::string_view key) override;
    IReadArchive& BeginArray(std::string_view key, std::size_t& count) override;
    IReadArchive& End() override;
    bool HasField(std::string_view key) const override;
    bool Ok() const override { return IsOk; }
    bool IsText() const override { return false; }
    void MarkMissingField(std::string_view key) override;
    void MarkInvalidField(std::string_view key) override;

private:
    BinaryReader& Reader;
    bool IsOk = true;
};
