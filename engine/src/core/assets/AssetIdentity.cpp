#include <core/assets/AssetIdentity.h>

#include <core/io/FileBytes.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace
{
constexpr std::array<std::string_view, 5> kDocumentsStatingTheirId = { ".smat", ".sscene", ".sdata", ".sworld",
                                                                       ".sanimscenario" };
} // namespace

bool StatesIdInside(std::string_view extension)
{
    return std::ranges::find(kDocumentsStatingTheirId, extension) != kDocumentsStatingTheirId.end();
}

std::filesystem::path SidecarOf(const std::filesystem::path& file)
{
    std::filesystem::path sidecar = file;
    sidecar += std::string(kAssetSidecarSuffix);
    return sidecar;
}

std::filesystem::path IdStatementOf(const std::filesystem::path& file)
{
    return StatesIdInside(file.extension().generic_string()) ? file : SidecarOf(file);
}

std::optional<AssetId> StatedAssetId(const JsonValue& root)
{
    const JsonValue* id = root.IsObject() ? root.Find(kAssetIdKey) : nullptr;
    return id != nullptr && id->IsString() ? AssetIdFromString(id->AsString()) : std::nullopt;
}

void ForgetStatedAssetId(JsonValue& root)
{
    if (root.IsObject())
        std::erase_if(root.AsObject(), [](const auto& member) { return member.first == kAssetIdKey; });
}

std::optional<AssetId> StatedAssetIdIn(std::string_view text)
{
    const std::optional<JsonValue> root = JsonParse(text);
    return root.has_value() ? StatedAssetId(*root) : std::nullopt;
}

std::optional<AssetId> ReadStatedAssetId(const std::filesystem::path& file)
{
    std::vector<std::byte> bytes;
    if (!ReadFileBytes(IdStatementOf(file), bytes))
        return std::nullopt;
    return StatedAssetIdIn(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
