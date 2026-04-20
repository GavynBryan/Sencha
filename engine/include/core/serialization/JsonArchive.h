#pragma once

#include <core/serialization/Archive.h>

#include <string>
#include <vector>

//=============================================================================
// JsonWriteArchive
//
// IWriteArchive that builds a JsonValue tree. BeginObject/BeginArray push a
// context onto a stack; End pops it and attaches the completed value to its
// parent. Call TakeValue() after the root write to retrieve the result.
//=============================================================================
class JsonWriteArchive final : public IWriteArchive
{
public:
    IWriteArchive& Field(std::string_view key, bool value) override;
    IWriteArchive& Field(std::string_view key, float value) override;
    IWriteArchive& Field(std::string_view key, double value) override;
    IWriteArchive& Field(std::string_view key, std::uint32_t value) override;
    IWriteArchive& Field(std::string_view key, std::string_view value) override;
    IWriteArchive& BeginObject(std::string_view key) override;
    IWriteArchive& BeginArray(std::string_view key, std::size_t count) override;
    IWriteArchive& End() override;
    bool Ok() const override { return IsOk; }
    bool IsText() const override { return true; }

    [[nodiscard]] JsonValue TakeValue();

private:
    struct Context
    {
        std::string Key;
        JsonValue Value;
        bool IsArray = false;
    };

    void AddValue(std::string_view key, JsonValue value);

    JsonValue Root;
    std::vector<Context> Stack;
    bool HasRoot = false;
    bool IsOk = true;
};

//=============================================================================
// JsonReadArchive
//
// IReadArchive that reads from an existing JsonValue tree. BeginObject/BeginArray
// push the target node onto a stack; array reads advance an index cursor so
// fields are consumed in declaration order regardless of key.
//=============================================================================
class JsonReadArchive final : public IReadArchive
{
public:
    explicit JsonReadArchive(const JsonValue& root);

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
    bool IsText() const override { return true; }
    void MarkMissingField(std::string_view key) override;
    void MarkInvalidField(std::string_view key) override;

private:
    struct Context
    {
        const JsonValue* Value = nullptr;
        bool IsArray = false;
        mutable std::size_t Index = 0;
    };

    [[nodiscard]] const JsonValue* Current() const;
    [[nodiscard]] const JsonValue* PeekValue(std::string_view key) const;
    [[nodiscard]] const JsonValue* TakeValue(std::string_view key);

    const JsonValue& Root;
    std::vector<Context> Stack;
    bool IsOk = true;
};
