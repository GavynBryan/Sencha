#include <assets/data/DataAssetSubtype.h>

#include <core/assets/AssetSource.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <cstddef>
#include <optional>
#include <vector>

std::string PeekDataAssetSubtype(std::string_view json)
{
    const std::optional<JsonValue> root = JsonParse(json);
    if (!root.has_value() || !root->IsObject())
        return {};

    const JsonValue* type = root->Find("type");
    return type != nullptr && type->IsString() ? type->AsString() : std::string{};
}

std::string PeekDataAssetSubtype(IAssetSource& source, const AssetRecord& record)
{
    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
        return {};

    return PeekDataAssetSubtype(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
