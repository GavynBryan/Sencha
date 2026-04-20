#include <core/serialization/JsonArchive.h>

#include <limits>

namespace
{
    JsonValue Number(double value)
    {
        return JsonValue(value);
    }
}

IWriteArchive& JsonWriteArchive::Field(std::string_view key, bool value)
{
    AddValue(key, JsonValue(value));
    return *this;
}

IWriteArchive& JsonWriteArchive::Field(std::string_view key, float value)
{
    AddValue(key, Number(static_cast<double>(value)));
    return *this;
}

IWriteArchive& JsonWriteArchive::Field(std::string_view key, double value)
{
    AddValue(key, Number(value));
    return *this;
}

IWriteArchive& JsonWriteArchive::Field(std::string_view key, std::uint32_t value)
{
    AddValue(key, Number(static_cast<double>(value)));
    return *this;
}

IWriteArchive& JsonWriteArchive::Field(std::string_view key, std::string_view value)
{
    AddValue(key, JsonValue(std::string(value)));
    return *this;
}

IWriteArchive& JsonWriteArchive::BeginObject(std::string_view key)
{
    Stack.push_back(Context{ std::string(key), JsonValue(JsonValue::Object{}), false });
    return *this;
}

IWriteArchive& JsonWriteArchive::BeginArray(std::string_view key, std::size_t count)
{
    JsonValue::Array array;
    array.reserve(count);
    Stack.push_back(Context{ std::string(key), JsonValue(std::move(array)), true });
    return *this;
}

IWriteArchive& JsonWriteArchive::End()
{
    if (Stack.empty())
    {
        IsOk = false;
        return *this;
    }

    Context complete = std::move(Stack.back());
    Stack.pop_back();
    AddValue(complete.Key, std::move(complete.Value));
    return *this;
}

void JsonWriteArchive::MarkInvalidField(std::string_view)
{
    IsOk = false;
}

JsonValue JsonWriteArchive::TakeValue()
{
    if (!Stack.empty())
        IsOk = false;
    return std::move(Root);
}

void JsonWriteArchive::AddValue(std::string_view key, JsonValue value)
{
    if (Stack.empty())
    {
        Root = std::move(value);
        HasRoot = true;
        return;
    }

    JsonValue& parent = Stack.back().Value;
    if (Stack.back().IsArray)
    {
        parent.AsArray().emplace_back(std::move(value));
        return;
    }

    parent.AsObject().emplace_back(std::string(key), std::move(value));
}

JsonReadArchive::JsonReadArchive(const JsonValue& root)
    : Root(root)
{
}

IReadArchive& JsonReadArchive::Field(std::string_view key, bool& value)
{
    const JsonValue* json = TakeValue(key);
    if (!json || !json->IsBool())
    {
        IsOk = false;
        return *this;
    }

    value = json->AsBool();
    return *this;
}

IReadArchive& JsonReadArchive::Field(std::string_view key, float& value)
{
    const JsonValue* json = TakeValue(key);
    if (!json || !json->IsNumber())
    {
        IsOk = false;
        return *this;
    }

    value = static_cast<float>(json->AsNumber());
    return *this;
}

IReadArchive& JsonReadArchive::Field(std::string_view key, double& value)
{
    const JsonValue* json = TakeValue(key);
    if (!json || !json->IsNumber())
    {
        IsOk = false;
        return *this;
    }

    value = json->AsNumber();
    return *this;
}

IReadArchive& JsonReadArchive::Field(std::string_view key, std::uint32_t& value)
{
    const JsonValue* json = TakeValue(key);
    if (!json || !json->IsNumber() || json->AsNumber() < 0.0
        || json->AsNumber() > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
    {
        IsOk = false;
        return *this;
    }

    value = static_cast<std::uint32_t>(json->AsNumber());
    return *this;
}

IReadArchive& JsonReadArchive::Field(std::string_view key, std::string& value)
{
    const JsonValue* json = TakeValue(key);
    if (!json || !json->IsString())
    {
        IsOk = false;
        return *this;
    }

    value = json->AsString();
    return *this;
}

IReadArchive& JsonReadArchive::BeginObject(std::string_view key)
{
    const JsonValue* json = TakeValue(key);
    if (!json || !json->IsObject())
    {
        IsOk = false;
        Stack.push_back(Context{});
        return *this;
    }

    Stack.push_back(Context{ json, false, 0 });
    return *this;
}

IReadArchive& JsonReadArchive::BeginArray(std::string_view key, std::size_t& count)
{
    const JsonValue* json = TakeValue(key);
    if (!json || !json->IsArray())
    {
        IsOk = false;
        Stack.push_back(Context{});
        count = 0;
        return *this;
    }

    count = json->AsArray().size();
    Stack.push_back(Context{ json, true, 0 });
    return *this;
}

IReadArchive& JsonReadArchive::End()
{
    if (Stack.empty())
    {
        IsOk = false;
        return *this;
    }

    Stack.pop_back();
    return *this;
}

bool JsonReadArchive::HasField(std::string_view key) const
{
    return PeekValue(key) != nullptr;
}

bool JsonReadArchive::IsString(std::string_view key) const
{
    const JsonValue* value = PeekValue(key);
    return value && value->IsString();
}

bool JsonReadArchive::IsObject(std::string_view key) const
{
    const JsonValue* value = PeekValue(key);
    return value && value->IsObject();
}

void JsonReadArchive::MarkMissingField(std::string_view)
{
    IsOk = false;
}

void JsonReadArchive::MarkInvalidField(std::string_view)
{
    IsOk = false;
}

const JsonValue* JsonReadArchive::Current() const
{
    return Stack.empty() ? &Root : Stack.back().Value;
}

const JsonValue* JsonReadArchive::PeekValue(std::string_view key) const
{
    const JsonValue* current = Current();
    if (!current)
        return nullptr;

    if (Stack.empty())
        return key.empty() ? current : current->Find(key);

    const Context& context = Stack.back();
    if (context.IsArray)
    {
        if (!current->IsArray() || context.Index >= current->AsArray().size())
            return nullptr;
        return &current->AsArray()[context.Index];
    }

    return current->IsObject() ? current->Find(key) : nullptr;
}

const JsonValue* JsonReadArchive::TakeValue(std::string_view key)
{
    const JsonValue* value = PeekValue(key);
    if (!Stack.empty() && Stack.back().IsArray && value)
        ++Stack.back().Index;
    return value;
}
